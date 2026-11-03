#include "protect.h"

#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hook.h"
#include "jni_bridge.h"
#include "log.h"
#include "targets.h"

namespace {

// The build step writes the hash of every non-writable PT_LOAD segment after
// this marker. The marker itself lives in the writable data segment, so writing
// the hash does not change the value it describes.
struct IntegrityBlock {
    char marker[16];
    uint64_t hash;
};
volatile IntegrityBlock integrityBlock = {"BOKUINTEGRITYv1", 0};

// Processes that are allowed to inspect this one. Add your own injector or
// debugger here; any other same-UID process or tracer is rejected. System
// binaries are trusted by path, so adbd and the Android daemons do not need an
// entry.
const char* const kFriendProcesses[] = {
    "bin.mt.plus",
    "magiskd",
    "su",
    "adbd",
};

void* libraryBase = nullptr;
char libraryPath[1024] = {};
char packageName[128] = {};

const char* baseName(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool libraryInfo() {
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&libraryInfo), &info) || !info.dli_fbase || !info.dli_fname)
        return false;
    libraryBase = info.dli_fbase;
    snprintf(libraryPath, sizeof(libraryPath), "%s", info.dli_fname);
    char* deleted = strstr(libraryPath, " (deleted)");
    if (deleted)
        *deleted = '\0';
    return true;
}

bool integrityOk() {
    if (!libraryBase)
        return false;
    const auto* eh = static_cast<const Elf64_Ehdr*>(libraryBase);
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB || eh->e_machine != EM_AARCH64)
        return false;

    const auto* ph =
        reinterpret_cast<const Elf64_Phdr*>(static_cast<const uint8_t*>(libraryBase) + eh->e_phoff);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || (ph[i].p_flags & PF_W))
            continue;
        const auto* start = static_cast<const uint8_t*>(libraryBase) + ph[i].p_vaddr;
        for (Elf64_Xword j = 0; j < ph[i].p_filesz; j++)
            hash = (hash ^ start[j]) * 0x100000001b3ULL;
    }

    uint64_t expected = integrityBlock.hash;
    return expected != 0 && hash == expected;
}

bool readStatus(pid_t pid, uid_t& uid, pid_t& tracer) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* file = fopen(path, "r");
    if (!file)
        return false;

    bool haveUid = false;
    bool haveTracer = false;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        unsigned int value = 0;
        int tracerPid = 0;
        if (!strncmp(line, "Uid:", 4) && sscanf(line + 4, "%u", &value) == 1) {
            uid = static_cast<uid_t>(value);
            haveUid = true;
        } else if (!strncmp(line, "TracerPid:", 10) && sscanf(line + 10, "%d", &tracerPid) == 1) {
            tracer = static_cast<pid_t>(tracerPid);
            haveTracer = true;
        }
    }
    fclose(file);
    return haveUid && haveTracer;
}

bool readCmdline(pid_t pid, char* out, size_t size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0)
        return false;
    out[n] = '\0';
    return true;
}

bool readExe(pid_t pid, char* out, size_t size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    ssize_t n = readlink(path, out, size - 1);
    if (n <= 0)
        return false;
    out[n] = '\0';
    return true;
}

bool matchesBase(const char* path, const char* name) {
    if (!path || !*path)
        return false;
    return !strcmp(baseName(path), name);
}

bool systemPath(const char* path) {
    if (!path || path[0] != '/')
        return false;
    static const char* const roots[] = {
        "/system/", "/system_ext/", "/vendor/", "/product/", "/apex/", "/odm/",
    };
    for (const char* root : roots) {
        if (!strncmp(path, root, strlen(root)))
            return true;
    }
    return !strcmp(path, "/init");
}

bool friendProcess(pid_t pid, const char* cmd, const char* exe, bool tracer) {
    if (pid == getpid())
        return true;
    if (!cmd[0] && !exe[0])
        return true;
    if (packageName[0] && !strncmp(cmd, packageName, strlen(packageName)))
        return true;
    if (tracer && systemPath(exe))
        return true;
    for (const char* name : kFriendProcesses) {
        if (matchesBase(cmd, name) || matchesBase(exe, name))
            return true;
    }
    return false;
}

bool friendDirectory(const char* path) {
    // An injector stages its payload and helper libraries here. Treating the
    // whole directory as friendly keeps a normal on-device injector working;
    // the process check still rejects an unexpected tracer.
    if (!strncmp(path, "/data/local/tmp/", 16))
        return true;
    return strstr(path, "/bin.mt.plus/") != nullptr;
}

bool mappingAllowed(const char* path) {
    if (systemPath(path) || friendDirectory(path))
        return true;
    if (!strncmp(path, "/data/dalvik-cache/", 19))
        return true;
    if (packageName[0] && strstr(path, packageName)) {
        if (!strncmp(path, "/data/app/", 10) || !strncmp(path, "/data/data/", 11) ||
            !strncmp(path, "/data/user/", 11) || !strncmp(path, "/data/user_de/", 14))
            return true;
    }
    if (!strcmp(path, libraryPath))
        return true;

    struct stat a;
    struct stat b;
    if (stat(libraryPath, &a) == 0 && stat(path, &b) == 0 && a.st_dev == b.st_dev &&
        a.st_ino == b.st_ino)
        return true;
    return false;
}

bool mappingsOk() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return false;

    char line[2048];
    bool ok = true;
    while (fgets(line, sizeof(line), maps)) {
        char perms[8] = {};
        char path[1800] = {};
        unsigned long start = 0;
        unsigned long end = 0;
        int n = sscanf(line, "%lx-%lx %7s %*s %*s %*s %1799[^\n]", &start, &end, perms, path);
        if (n < 3 || perms[2] != 'x')
            continue;

        char* p = path;
        while (*p == ' ')
            p++;
        if (*p != '/')
            continue;
        // ART maps its JIT code cache from a memfd, which starts with a slash
        // but is not a file an attacker can place.
        if (!strncmp(p, "/memfd:", 7))
            continue;
        char* newline = strchr(p, '\n');
        if (newline)
            *newline = '\0';
        char* deleted = strstr(p, " (deleted)");
        if (deleted)
            *deleted = '\0';
        if (!mappingAllowed(p)) {
            ok = false;
            break;
        }
    }
    fclose(maps);
    return ok;
}

} // namespace

bool initProtection() {
    if (!libraryInfo()) {
        LOGE("Protection: library path unavailable");
        return false;
    }
    appPackage(packageName, sizeof(packageName));
    if (!initTargets(baseName(libraryPath))) {
        LOGE("Protection: library name mismatch");
        return false;
    }
    if (!integrityOk()) {
        LOGE("Protection: code integrity check failed");
        return false;
    }
    return true;
}

bool protectionCheck() {
    if (!libraryBase || !integrityOk())
        return false;

    pid_t tracer = 0;
    uid_t selfUid = 0;
    if (!readStatus(getpid(), selfUid, tracer))
        return false;
    if (tracer > 0) {
        char cmd[256] = {};
        char exe[512] = {};
        readCmdline(tracer, cmd, sizeof(cmd));
        readExe(tracer, exe, sizeof(exe));
        if (!friendProcess(tracer, cmd, exe, true))
            return false;
    }

    DIR* proc = opendir("/proc");
    if (!proc)
        return false;
    while (struct dirent* entry = readdir(proc)) {
        if (!isdigit(static_cast<unsigned char>(entry->d_name[0])))
            continue;
        pid_t pid = static_cast<pid_t>(atoi(entry->d_name));
        if (pid == getpid())
            continue;

        uid_t uid = 0;
        pid_t childTracer = 0;
        if (!readStatus(pid, uid, childTracer) || uid != selfUid)
            continue;

        char cmd[256] = {};
        char exe[512] = {};
        readCmdline(pid, cmd, sizeof(cmd));
        readExe(pid, exe, sizeof(exe));
        if (!friendProcess(pid, cmd, exe, false)) {
            closedir(proc);
            return false;
        }
    }
    closedir(proc);
    return mappingsOk();
}

void startProtectionWatchdog() {
    pthread_t thread;
    int created = pthread_create(
        &thread, nullptr,
        [](void*) -> void* {
            prctl(PR_SET_NAME, "boku-watchdog", 0, 0, 0);
            for (;;) {
                sleep(15);
                if (!protectionCheck()) {
                    unhookAll();
                    break;
                }
            }
            return nullptr;
        },
        nullptr);
    if (created != 0) {
        LOGE("Protection: watchdog failed to start");
        return;
    }
    pthread_detach(thread);
}
