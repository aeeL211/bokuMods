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

// The log lives only in the app's external files dir, i.e.
// Android/data/<package>/files/bokumods.log. The path comes from
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
            snprintf(out, size, "%s/bokumods.log", chars);
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
// build the same path from the process package and the storage root. The
// package dir exists from install time, so a missing or unwritable one means
// the guess is wrong (renamed package, clone, other mount) and creating it
// would hide the log outside the app sandbox.
bool legacyDir(char* out, size_t size) {
    char package[192];
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, package, sizeof(package) - 1);
    close(fd);
    if (n <= 0)
        return false;
    package[n] = '\0';
    char* colon = strchr(package, ':');
    if (colon)
        *colon = '\0';
    if (!package[0])
        return false;

    char root[512];
    if (!externalStorageRoot(root, sizeof(root)))
        return false;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/Android/data/%s/files", root, package);
    if (access(dir, W_OK | X_OK) != 0)
        return false;
    snprintf(out, size, "%s/bokumods.log", dir);
    return true;
}

// Deleting the previous log only helps when the file itself blocks the append.
// A missing directory, a full disk, or an unavailable mount is not fixed by
// removing data, so the destructive step is limited to permission errors on an
// existing regular file.
bool isFile(const char* path) {
    struct stat st = {};
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// A previous session can leave a file this process may not append to (other
// owner or read-only mode). Plain open() would then fail on every run and the
// log would look dead, so repair the file step by step instead of giving up on
// the first error.
int openPath(const char* path, int* err) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd >= 0)
        return fd;
    *err = errno;
    if (*err != EACCES && *err != EPERM)
        return -1;

    chmod(path, 0666);
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd >= 0)
        return fd;
    *err = errno;
    if ((*err != EACCES && *err != EPERM) || !isFile(path))
        return -1;

    unlink(path);
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd >= 0)
        return fd;
    *err = errno;
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
    char path[512];
    bool fromActivity = filesDir(path, sizeof(path));
    if (!fromActivity && !legacyDir(path, sizeof(path)))
        return;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0777);
    }

    int err = 0;
    int fd = openPath(path, &err);
    char used[512];
    snprintf(used, sizeof(used), "%s", path);
    if (fd < 0) {
        // The path itself may be unusable, but the directory is ours: a second name
        // still gives us a log for this run.
        snprintf(used, sizeof(used), "%s/bokumods-2.log", dir);
        fd = openPath(used, &err);
        if (fd < 0)
            return;
    }
    logFd = fd;

    char clock[16];
    char date[16];
    formatTime(clock, sizeof(clock), "%H:%M:%S");
    formatTime(date, sizeof(date), "%Y-%m-%d");
    char line[640];
    int n = snprintf(line, sizeof(line), "[%s] INFO  Log start (%s)\n", clock, date);
    if (n > 0)
        write(fd, line, static_cast<size_t>(n));
    n = snprintf(line, sizeof(line), "[%s] INFO  Log file: %s (append%s)\n", clock, used,
                 fromActivity ? "" : ", package fallback");
    if (n > 0)
        write(fd, line, static_cast<size_t>(n));
    if (strcmp(used, path) != 0) {
        n = snprintf(line, sizeof(line), "[%s] ERROR Log file: %s unusable (%d %s), using %s\n",
                     clock, path, err, strerror(err), used);
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
