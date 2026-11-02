#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Each account detail row asks this before it decides what to show. Answering
// yes keeps the owner's "hide" choice from replacing the value.
bool managerHook(void*, int, void*, bool, void*) { return true; }

// The friend, blacklist, add-friend and verify screens carry their own copy of
// the same check, so every one of them is answered the same way.
bool rowHook(void*, int, void*) { return true; }

Field userSelectField = {};
Field uidField = {};

// The email row formats whatever the fetched profile carried. A profile never
// carries the email of another account, so an unblocked row would print
// nothing. The account id stands in, so the row still shows a value.
void emailHook(void* self, void* objs, void* labels, void* text, void* method) {
    auto orig =
        reinterpret_cast<void (*)(void*, void*, void*, void*, void*)>(target(T_ACCOUNT_EMAIL).orig);
    if (!orig)
        return;

    void* shown = text;
    if (!shown || stringLength(shown) == 0) {
        void* user = readPtr(self, userSelectField);
        void* uid = user ? readPtr(user, uidField) : nullptr;
        if (uid && stringLength(uid) > 0)
            shown = uid;
    }
    orig(self, objs, labels, shown, method);
}

bool resolvePrivacyFields() {
    return resolveField(classOf("Net__Account__Manager"), "user_Select", userSelectField) &&
           resolveField(fieldClass(userSelectField), "uid", uidField);
}

} // namespace

void installPrivacyHooks() {
    if (resolvePrivacyFields()) {
        LOGI("Privacy fields OK");
    } else {
        LOGE("Privacy fields not found; a missing email stays blank");
    }

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
    installHook(target(T_ACCOUNT_EMAIL), reinterpret_cast<void*>(emailHook), "Manager.Email");
}
