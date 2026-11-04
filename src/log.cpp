#include "log.h"

#include <dirent.h>
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

#include "hook.h"
#include "jni_bridge.h"

#ifdef BOKU_LOG

namespace {

// Each session writes its own file in the app's external files dir, i.e.
// Android/data/<package>/files/bokumods-1.log, bokumods-2.log and so on. The
// path comes from Context.getExternalFilesDir() through UnityPlayer.currentActivity,
// so the package name is never hardcoded and a renamed package still works.
std::atomic<int> logFd{-1};

// The path of the file this session opened, kept so a command can print it.
char sessionPath[600] = {};

// The activity, and with it the log dir, only exists a few seconds into the
// process. Lines from that window are kept here and flushed when the file
// finally opens, so the start of a session is not lost.
constexpr int kPendingMax = 32;
constexpr int kPendingLen = 220;
char pending[kPendingMax][kPendingLen] = {};
std::atomic<int> pendingCount{0};

// The worker, JNI_OnLoad and the constructor all log before the file exists, so
// the pending ring needs its own lock. The signal handler never takes it: it
// writes straight to logFd, which is already open by the time a crash matters.
std::atomic_flag pendingLock = ATOMIC_FLAG_INIT;

void lockPending() {
    while (pendingLock.test_and_set(std::memory_order_acquire)) {
    }
}

void unlockPending() { pendingLock.clear(std::memory_order_release); }

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
    // Re-check under the lock: the opener can install logFd between an earlier
    // check and this store, and a line left behind after the final flush would
    // never reach the file.
    lockPending();
    int fd = logFd;
    if (fd >= 0) {
        unlockPending();
        write(fd, line, static_cast<size_t>(n));
        return;
    }
    // Full buffer drops the oldest line instead of blocking the caller.
    int slot = pendingCount.fetch_add(1) % kPendingMax;
    if (slot < 0)
        slot = 0;
    snprintf(pending[slot], kPendingLen, "%.*s", n, line);
    unlockPending();
}

void flush() {
    lockPending();
    int count = pendingCount.exchange(0);
    if (count > kPendingMax)
        count = kPendingMax;
    for (int i = 0; i < count; i++) {
        size_t len = strlen(pending[i]);
        if (len)
            write(logFd, pending[i], len);
    }
    unlockPending();
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
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getDir = activityClass ? env->GetMethodID(activityClass, "getExternalFilesDir",
                                                            "(Ljava/lang/String;)Ljava/io/File;")
                                         : nullptr;
        if (getDir)
            dir = env->CallObjectMethod(activity, getDir, nullptr);
        if (activityClass)
            env->DeleteLocalRef(activityClass);
    }
    if (dir) {
        jclass fileClass = env->FindClass("java/io/File");
        if (fileClass) {
            jmethodID getPath =
                env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
            if (getPath)
                path = static_cast<jstring>(env->CallObjectMethod(dir, getPath));
            env->DeleteLocalRef(fileClass);
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

// A session file carries the number of the session, so the name stays short and
// the highest number in the folder is always the newest one.
int sessionNumber(const char* name) {
    int value = 0;
    int used = 0;
    if (sscanf(name, "bokumods-%d.log%n", &value, &used) != 1)
        return 0;
    if (used <= 0 || name[used] != '\0' || value < 1)
        return 0;
    return value;
}

int lastSession(const char* dir) {
    DIR* handle = opendir(dir);
    if (!handle)
        return 0;
    int last = 0;
    while (struct dirent* entry = readdir(handle)) {
        int value = sessionNumber(entry->d_name);
        if (value > last)
            last = value;
    }
    closedir(handle);
    return last;
}

// One file per session, numbered in the order the sessions start. O_EXCL never
// touches an earlier file, and the number only grows, so a deleted file cannot
// hand its number to a later session.
int openNew(const char* dir, char* path, size_t pathSize, int* err) {
    int first = lastSession(dir) + 1;
    for (int i = first; i < first + 100; i++) {
        snprintf(path, pathSize, "%s/bokumods-%d.log", dir, i);
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
// and the handler only compares numbers. Only executable segments are kept, so
// a data word that happens to sit in a loaded module is not read as code.
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
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X))
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

// The caller of abort() and __stack_chk_fail() is recorded by the probes below,
// because the PC of the signal itself only ever lands inside libc and the frame
// scan has no frame pointers to follow. A non-zero value is resolved through the
// same module table, so the next SIGABRT names the code that gave up.
std::atomic<uintptr_t> abortCaller{0};
std::atomic<uintptr_t> stackCheckCaller{0};
void* abortOriginal = nullptr;
void* stackCheckOriginal = nullptr;

bool writeCodeFrame(int fd, const char* clock, const char* label, uintptr_t address) {
    for (int m = 0; m < moduleCount; m++) {
        if (address < modules[m].start || address >= modules[m].end)
            continue;
        char line[512];
        int n = snprintf(line, sizeof(line), "[%s] ERROR CRASH %s %s+0x%lx\n", clock, label,
                         modules[m].name, static_cast<unsigned long>(address - modules[m].start));
        if (n > 0 && fd >= 0)
            write(fd, line, static_cast<size_t>(n));
        return true;
    }
    return false;
}

using FatalFn = void (*)(void);

// Unhandled exceptions, failed assertions and heap checks all end in abort(),
// so the caller identifies the component that killed the process.
void abortProbe() {
    abortCaller.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    auto orig = reinterpret_cast<FatalFn>(abortOriginal);
    if (orig)
        orig();
    _exit(127);
}

// The game's own code is built with the stack protector, so a smashed canary
// names the function that wrote past its frame.
void stackCheckProbe() {
    stackCheckCaller.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    auto orig = reinterpret_cast<FatalFn>(stackCheckOriginal);
    if (orig)
        orig();
    _exit(127);
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
                     "[%s] ERROR CRASH signal=%s code=%d pc=0x%lx module=%s+0x%lx "
                     "fault=0x%lx lr=0x%lx x19=0x%lx x20=0x%lx\n",
                     clock, signalName(sig), info->si_code, static_cast<unsigned long>(pc), name,
                     static_cast<unsigned long>(offset), static_cast<unsigned long>(fault),
                     static_cast<unsigned long>(lr),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[19]),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[20]));
    int fd = logFd;
    if (n > 0 && fd >= 0)
        write(fd, line, static_cast<size_t>(n));

    // A probe knows where abort() was called from, which is the only frame that
    // matters when IL2CPP or a C++ runtime decides to end the process.
    writeCodeFrame(fd, clock, "abort caller", abortCaller.load(std::memory_order_relaxed));
    writeCodeFrame(fd, clock, "stack check caller",
                   stackCheckCaller.load(std::memory_order_relaxed));

    // Only words that fall inside an executable module segment are printed, so
    // heap values, module data and stale stack words stay out of the log.
    uintptr_t sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 16; i++) {
        uintptr_t candidate = reinterpret_cast<const uintptr_t*>(sp)[i];
        if (writeCodeFrame(fd, clock, "frame", candidate))
            shown++;
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

const char* logFilePath() { return sessionPath[0] ? sessionPath : nullptr; }

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
    snprintf(sessionPath, sizeof(sessionPath), "%s", path);

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

// Both probes hook libc directly, which no target names, so each install
// failure only leaves that one frame out of the next crash block.
void installCrashProbes() {
    void* abortAddr = dlsym(RTLD_DEFAULT, "abort");
    installProbeHook(abortAddr, reinterpret_cast<void*>(abortProbe), &abortOriginal, "abort");
    void* stackCheckAddr = dlsym(RTLD_DEFAULT, "__stack_chk_fail");
    installProbeHook(stackCheckAddr, reinterpret_cast<void*>(stackCheckProbe), &stackCheckOriginal,
                     "__stack_chk_fail");
}

#else

// log.h provides the no-op stubs; this keeps the translation unit non-empty.
static_assert(true, "");

#endif
