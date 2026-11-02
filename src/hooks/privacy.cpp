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
Field emailField = {};
Field verifyAccountField = {};

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

// The verify detail fills its email row straight from the selected user, not
// through the formatter above. The account id is written for the duration of
// the refresh, then the original value is put back.
void verifyRefreshHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_VERIFY_REFRESH).orig);
    if (!orig)
        return;

    void* account = readPtr(self, verifyAccountField);
    void* user = readPtr(account, userSelectField);
    void* saved = readPtr(user, emailField);
    bool replaced = false;
    if (user && stringLength(saved) == 0) {
        void* uid = readPtr(user, uidField);
        if (uid && stringLength(uid) > 0) {
            setRef(user, emailField, uid);
            replaced = true;
        }
    }

    orig(self, method);

    if (replaced)
        setRef(user, emailField, saved);
}

bool resolvePrivacyFields() {
    void* manager = classOf("Net__Account__Manager");
    void* verifyDetail = classOf("Net__Account__Verify__Detail");
    return resolveField(manager, "user_Select", userSelectField) &&
           resolveField(fieldClass(userSelectField), "uid", uidField) &&
           resolveField(fieldClass(userSelectField), "email", emailField) &&
           resolveField(verifyDetail, "account", verifyAccountField);
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
    installHook(target(T_VERIFY_REFRESH), reinterpret_cast<void*>(verifyRefreshHook),
                "Verify__Detail.Refresh");
}
