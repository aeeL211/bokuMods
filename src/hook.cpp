#include "hook.h"

#include <shadowhook.h>

#include "log.h"

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

bool installHook(Target& t, void* replacement, const char* label) {
    if (!t.addr) {
        LOGE("Target not found: %s", label);
        return false;
    }
    void* stub = shadowhook_hook_func_addr(t.addr, replacement, &t.orig);
    if (stub) {
        LOGI("Hooked %s at %p (a64)", label, t.addr);
        return true;
    }
    int e = shadowhook_get_errno();
    LOGE("Hook failed: %s (error %d, %s)", label, e, shadowhook_to_errmsg(e));
    return false;
}
