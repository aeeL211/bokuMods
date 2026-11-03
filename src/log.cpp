#include "log.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "jni_bridge.h"

#ifdef BOKU_LOG

namespace {

// Each session writes its own file in the app's external files dir, i.e.
// Android/data/<package>/files/YYYYMMDD-HHMMSS.log. The path comes from
// Context.getExternalFilesDir() through UnityPlayer.currentActivity, so the
// package name is never hardcoded and a renamed package still works.
std::atomic<int> logFd{-1};

// The activity, and with it the log dir, only exists a few seconds into the
// process. Lines from that window are kept here and flushed when the file
// finally opens, so the start of a session is not lost.
constexpr int kPendingMax = 32;
constexpr int kPendingLen = 220;
char pending[kPendingMax][kPendingLen] = {};
std::atomic<int> pendingCount{0};

void formatTime(char* out, size_t size, const char* pattern) {
    std::time_t now = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&now, &tm);
    std::strftime(out, size, pattern, &tm);
}

// INFO is padded to five characters so the message column lines up with ERROR.
const char* levelName(char level) { return level == 'E' ? "ERROR" : "INFO "; }

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    default:
        return "UNKNOWN";
    }
}

void writeLine(const char* line, int n) {
    int fd = logFd;
    if (fd >= 0) {
        write(fd, line, static_cast<size_t>(n));
        return;
    }
    // Full buffer drops the oldest line instead of blocking the caller.
    int slot = pendingCount.fetch_add(1) % kPendingMax;
    if (slot < 0)
        slot = 0;
    snprintf(pending[slot], kPendingLen, "%.*s", n, line);
}

void flush() {
    int count = pendingCount.exchange(0);
    if (count > kPendingMax)
        count = kPendingMax;
    for (int i = 0; i < count; i++) {
        size_t len = strlen(pending[i]);
        if (len)
            write(logFd, pending[i], len);
    }
}

// getExternalFilesDir is the public API for the per-app external dir; calling it
// avoids guessing the package name or the storage mount point.
bool filesDir(char* out, size_t size) {
    JNIEnv* env = nullptr;
    bool detach = false;
    if (!attachJni(&env, &detach))
        return false;

    bool ok = false;
    jobject activity = nullptr;
    jobject dir = nullptr;
    jstring path = nullptr;
    const char* chars = nullptr;

    if (unityPlayer()) {
        jfieldID current =
            env->GetStaticFieldID(unityPlayer(), "currentActivity", "Landroid/app/Activity;");
        if (current)
            activity = env->GetStaticObjectField(unityPlayer(), current);
    }
    if (activity) {
        jmethodID getDir = env->GetMethodID(env->GetObjectClass(activity), "getExternalFilesDir",
                                            "(Ljava/lang/String;)Ljava/io/File;");
        if (getDir)
            dir = env->CallObjectMethod(activity, getDir, nullptr);
    }
    if (dir) {
        jclass fileClass = env->FindClass("java/io/File");
        if (fileClass) {
            jmethodID getPath =
                env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
            if (getPath)
                path = static_cast<jstring>(env->CallObjectMethod(dir, getPath));
        }
    }
    if (path) {
        chars = env->GetStringUTFChars(path, nullptr);
        if (chars) {
            snprintf(out, size, "%s", chars);
            ok = true;
        }
    }

    if (chars)
        env->ReleaseStringUTFChars(path, chars);
    if (path)
        env->DeleteLocalRef(path);
    if (dir)
        env->DeleteLocalRef(dir);
    if (activity)
        env->DeleteLocalRef(activity);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (detach)
        findJavaVm()->DetachCurrentThread();
    return ok;
}

// Fallback for the brief window before UnityPlayer.currentActivity is set:
// build the same dir from the process package and the storage root. The
// package dir exists from install time, so a missing or unwritable one means
// the guess is wrong (renamed package, clone, other mount) and creating it
// would hide the log outside the app sandbox.
bool legacyDir(char* out, size_t size) {
    char package[192];
    if (!appPackage(package, sizeof(package)))
        return false;

    char root[512];
    if (!externalStorageRoot(root, sizeof(root)))
        return false;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/Android/data/%s/files", root, package);
    if (access(dir, W_OK | X_OK) != 0)
        return false;
    snprintf(out, size, "%s", dir);
    return true;
}

// One file per session, named with the local open time. O_EXCL never touches an
// earlier file; two sessions opened in the same second get a counter instead of
// sharing a name.
int openNew(const char* dir, char* path, size_t pathSize, int* err) {
    char stamp[32];
    formatTime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S");
    for (int i = 0; i < 100; i++) {
        if (i == 0)
            snprintf(path, pathSize, "%s/%s.log", dir, stamp);
        else
            snprintf(path, pathSize, "%s/%s-%d.log", dir, stamp, i);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd >= 0)
            return fd;
        *err = errno;
        if (*err != EEXIST)
            return -1;
    }
    return -1;
}

bool ensureOpen() {
    if (logFd >= 0)
        return true;
    // The dir is not reachable until the activity exists, so retry at most every
    // two seconds instead of on every line.
    static std::atomic<long> lastTry{0};
    long now = static_cast<long>(std::time(nullptr));
    long prev = lastTry.load();
    if (now - prev < 2)
        return false;
    lastTry = now;
    logOpenFile();
    return logFd >= 0;
}

// Unhandled IL2CPP exceptions abort the process, so the PC of the last fault is
// the only reliable clue about where a crash came from. The previous handler
// (Unity installs one) is restored before re-raising, so its tombstone still
// happens.
struct SigSlot {
    int sig;
    struct sigaction prev;
};

// Return addresses on the crashing stack name the caller chain, but resolving
// them with dladdr() would take the linker lock in a process that is already
// broken. The module ranges are therefore snapshotted once, before any crash,
// and the handler only compares numbers.
struct Module {
    const char* name;
    uintptr_t start;
    uintptr_t end;
};

constexpr int kMaxModules = 32;
Module modules[kMaxModules] = {};
int moduleCount = 0;

int gatherModule(struct dl_phdr_info* info, size_t, void*) {
    if (moduleCount >= kMaxModules)
        return 1;
    if (!info->dlpi_name || !info->dlpi_name[0])
        return 0;
    uintptr_t start = UINTPTR_MAX;
    uintptr_t end = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD)
            continue;
        uintptr_t lo = info->dlpi_addr + ph.p_vaddr;
        uintptr_t hi = lo + ph.p_memsz;
        if (lo < start)
            start = lo;
        if (hi > end)
            end = hi;
    }
    if (end <= start)
        return 0;
    const char* slash = strrchr(info->dlpi_name, '/');
    modules[moduleCount].name = slash ? slash + 1 : info->dlpi_name;
    modules[moduleCount].start = start;
    modules[moduleCount].end = end;
    moduleCount++;
    return 0;
}

SigSlot sigSlots[] = {
    {SIGSEGV, {}},
    {SIGBUS, {}},
    {SIGILL, {}},
    {SIGABRT, {}},
};

void crashHandler(int sig, siginfo_t* info, void* context) {
    auto* uc = static_cast<ucontext_t*>(context);
    uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
    uintptr_t lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
    uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
    Dl_info dl = {};
    uintptr_t offset = 0;
    const char* name = "?";
    if (dladdr(reinterpret_cast<void*>(pc), &dl) && dl.dli_fbase) {
        name = dl.dli_fname;
        offset = pc - reinterpret_cast<uintptr_t>(dl.dli_fbase);
    }
    // x19/x20 are the end/begin pointers of the UTF-16 scan in libunity, so the
    // pair shows whether a bad string length sent it past the buffer.
    char clock[16];
    formatTime(clock, sizeof(clock), "%H:%M:%S");
    char line[512];
    int n = snprintf(line, sizeof(line),
                     "[%s] ERROR CRASH signal=%s pc=0x%lx module=%s+0x%lx fault=0x%lx lr=0x%lx "
                     "x19=0x%lx x20=0x%lx\n",
                     clock, signalName(sig), static_cast<unsigned long>(pc), name,
                     static_cast<unsigned long>(offset), static_cast<unsigned long>(fault),
                     static_cast<unsigned long>(lr),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[19]),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[20]));
    int fd = logFd;
    if (n > 0 && fd >= 0)
        write(fd, line, static_cast<size_t>(n));

    // Only words that fall inside a known module are printed, so heap values
    // and stale stack data stay out of the log.
    uintptr_t sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
    int shown = 0;
    for (int i = 0; i < 192 && shown < 10; i++) {
        uintptr_t candidate = reinterpret_cast<const uintptr_t*>(sp)[i];
        for (int m = 0; m < moduleCount; m++) {
            if (candidate < modules[m].start || candidate >= modules[m].end)
                continue;
            n = snprintf(line, sizeof(line), "[%s] ERROR CRASH frame %s+0x%lx\n", clock,
                         modules[m].name, static_cast<unsigned long>(candidate - modules[m].start));
            if (n > 0 && fd >= 0)
                write(fd, line, static_cast<size_t>(n));
            shown++;
            break;
        }
    }
    for (SigSlot& slot : sigSlots) {
        if (slot.sig == sig) {
            sigaction(sig, &slot.prev, nullptr);
            break;
        }
    }
    raise(sig);
}

} // namespace

void logOpenFile() {
    if (logFd >= 0)
        return;
    char dir[512];
    bool fromActivity = filesDir(dir, sizeof(dir));
    if (!fromActivity && !legacyDir(dir, sizeof(dir)))
        return;

    mkdir(dir, 0777);

    char path[600];
    int err = 0;
    int fd = openNew(dir, path, sizeof(path), &err);
    bool repaired = false;
    // The dir is the only thing this process may have to repair; when the
    // create still fails, the next ensureOpen retry runs two seconds later.
    if (fd < 0 && (err == EACCES || err == EPERM)) {
        chmod(dir, 0777);
        fd = openNew(dir, path, sizeof(path), &err);
        repaired = fd >= 0;
    }
    if (fd < 0)
        return;
    logFd = fd;

    char clock[16];
    char date[16];
    formatTime(clock, sizeof(clock), "%H:%M:%S");
    formatTime(date, sizeof(date), "%Y-%m-%d");
    char line[640];
    int n = snprintf(line, sizeof(line), "[%s] INFO  Log start (%s)\n", clock, date);
    if (n > 0)
        write(fd, line, static_cast<size_t>(n));
    // The dir is always Android/data/<package>/files, so the full path only
    // repeats what the reader already knows and makes the line hard to scan.
    const char* slash = strrchr(path, '/');
    const char* name = slash ? slash + 1 : path;
    n = snprintf(line, sizeof(line), "[%s] INFO  Log file: %s (new session%s)\n", clock, name,
                 fromActivity ? "" : ", package fallback");
    if (n > 0)
        write(fd, line, static_cast<size_t>(n));
    if (repaired) {
        n = snprintf(line, sizeof(line),
                     "[%s] ERROR Log dir was not writable (%d %s), mode fixed\n", clock, err,
                     strerror(err));
        if (n > 0)
            write(fd, line, static_cast<size_t>(n));
    }
    flush();
}

void logPrint(char level, const char* fmt, ...) {
    char msg[400];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char clock[16];
    formatTime(clock, sizeof(clock), "%H:%M:%S");
    char line[512];
    int n = snprintf(line, sizeof(line), "[%s] %s %s\n", clock, levelName(level), msg);
    if (n <= 0)
        return;
    // snprintf reports the untruncated length, so clamp before writing.
    if (n >= static_cast<int>(sizeof(line)))
        n = static_cast<int>(sizeof(line)) - 1;
    ensureOpen();
    writeLine(line, n);
}

void installCrashLog() {
    dl_iterate_phdr(gatherModule, nullptr);
    for (SigSlot& slot : sigSlots) {
        struct sigaction action = {};
        action.sa_sigaction = crashHandler;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(slot.sig, &action, &slot.prev);
    }
}

#else

// log.h provides the no-op stubs; this keeps the translation unit non-empty.
static_assert(true, "");

#endif
