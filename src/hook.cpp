#include "hook.h"

#include <shadowhook.h>

#include "log.h"

namespace {

int installedCount = 0;

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
    installedCount++;
    return true;
}

int installedHooks() { return installedCount; }
