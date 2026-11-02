#include "hooks.h"

#include "hook.h"
#include "targets.h"

namespace {

// Each account detail row asks this before it decides what to show. Answering
// yes keeps the owner's "hide" choice from replacing the value.
bool managerHook(void*, int, void*, bool, void*) { return true; }

// The friend, blacklist, add-friend and verify screens carry their own copy of
// the same check, so every one of them is answered the same way.
bool rowHook(void*, int, void*) { return true; }

} // namespace

void installPrivacyHooks() {
    installHook(target(T_PRIVACY_MANAGER), reinterpret_cast<void*>(managerHook),
                "Manager.Term__Privacy__Visible");
    installHook(target(T_PRIVACY_BLACKLIST), reinterpret_cast<void*>(rowHook),
                "Blacklist.Term__Privacy__Visible");
    installHook(target(T_PRIVACY_ADD_FRIEND), reinterpret_cast<void*>(rowHook),
                "Add_Friend.Term__Privacy__Visible");
    installHook(target(T_PRIVACY_FRIEND_PROFILE), reinterpret_cast<void*>(rowHook),
                "Friend_Profile.Term__Privacy__Visible");
    installHook(target(T_PRIVACY_VERIFY), reinterpret_cast<void*>(rowHook),
                "Verify.Term__Privacy__Visible");
    installHook(target(T_PRIVACY_VERIFY_EMAIL), reinterpret_cast<void*>(rowHook),
                "Verify.Term__Privacy__Visible__Email");
}
