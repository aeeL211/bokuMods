#include "hooks.h"

#include "hook.h"
#include "log.h"
#include "targets.h"

namespace {

// The host sends the starting permission set, and a guest cannot ask for more
// later, so all three flags are forced before the game stores them.
void permissionStartHook(void* self, bool build, bool destroy, bool, void* method) {
    LOGI("Permission: start game build=%d destroy=%d", build, destroy);
    auto orig =
        reinterpret_cast<void (*)(void*, bool, bool, bool, void*)>(target(T_PERMISSION_START).orig);
    if (!orig)
        return;
    orig(self, true, true, true, method);
}

void permissionSaveHook(void* self, bool, void* method) {
    LOGI("Permission: save world received");
    auto orig = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PERMISSION_SAVE).orig);
    if (!orig)
        return;
    orig(self, true, method);
}

} // namespace

void installMultiplayerHooks() {
    installHook(target(T_PERMISSION_START), reinterpret_cast<void*>(permissionStartHook),
                "Client.Permission__Start_Game");
    installHook(target(T_PERMISSION_SAVE), reinterpret_cast<void*>(permissionSaveHook),
                "Client.Permission__Save_World__Receive");
}
