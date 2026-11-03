#include "hook.h"

#include <pthread.h>
#include <shadowhook.h>

#include "log.h"

namespace {

struct Installed {
    Target* target;
    void* stub;
};

Installed installed[T_COUNT] = {};
int installedCount = 0;
pthread_mutex_t installedLock = PTHREAD_MUTEX_INITIALIZER;

} // namespace

// Init once, early: the mode cannot change after init and unique mode lets a
// hook call its trampoline directly. debuggable stays false because it writes to
// logcat, which this project does not use.
bool initShadowHook() {
    int e = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (e) {
        LOGE("ShadowHook init failed: %d %s", e, shadowhook_to_errmsg(e));
        return false;
    }
    LOGI("ShadowHook initialized");
    return true;
}

bool installHook(Target& t, void* replacement) {
    if (!t.addr) {
        LOGE("Target not found: %s.%s", t.cls, t.name);
        return false;
    }
    void* stub = shadowhook_hook_func_addr(t.addr, replacement, &t.orig);
    if (!stub) {
        int e = shadowhook_get_errno();
        LOGE("Hook failed: %s.%s (error %d, %s)", t.cls, t.name, e, shadowhook_to_errmsg(e));
        return false;
    }

    pthread_mutex_lock(&installedLock);
    if (installedCount < T_COUNT) {
        installed[installedCount].target = &t;
        installed[installedCount].stub = stub;
        installedCount++;
    }
    pthread_mutex_unlock(&installedLock);
    t.stub = stub;
    return true;
}

int installedHooks() {
    pthread_mutex_lock(&installedLock);
    int count = installedCount;
    pthread_mutex_unlock(&installedLock);
    return count;
}

void unhookAll() {
    pthread_mutex_lock(&installedLock);
    for (int i = installedCount - 1; i >= 0; i--) {
        Installed& entry = installed[i];
        if (!entry.stub)
            continue;
        shadowhook_unhook(entry.stub);
        entry.stub = nullptr;
        entry.target->stub = nullptr;
        // A hook body that is still in flight returns early instead of calling
        // a trampoline that no longer exists.
        entry.target->orig = nullptr;
    }
    int count = installedCount;
    installedCount = 0;
    pthread_mutex_unlock(&installedLock);
    if (count > 0)
        LOGE("Protection triggered: removed %d hooks", count);
}
