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

using SetFn = void (*)(void*, void*, void*, void*, bool, void*);
using SetLocalFn = void (*)(void*, void*, void*, void*);

// A fetched profile never carries the email of another account, so every row
// that prints it would stay blank. The account id stands in for it at the one
// place a user object is built, so no screen depends on refresh order.
void fillEmail(void* userRef) {
    void* user = userRef ? *reinterpret_cast<void**>(userRef) : nullptr;
    if (!user)
        return;

    void* email = readPtr(user, emailField);
    if (email && stringLength(email) > 0)
        return;

    void* uid = readPtr(user, uidField);
    if (uid && stringLength(uid) > 0)
        setRef(user, emailField, uid);
}

void setHook(void* self, void* userRef, void* uid, void* docs, bool overwrite, void* method) {
    auto orig = reinterpret_cast<SetFn>(target(T_USER_SET).orig);
    if (orig)
        orig(self, userRef, uid, docs, overwrite, method);
    fillEmail(userRef);
}

// The three local setters differ only by the type of their second argument.
void setLocal(TargetId id, void* self, void* userRef, void* data, void* method) {
    auto orig = reinterpret_cast<SetLocalFn>(target(id).orig);
    if (orig)
        orig(self, userRef, data, method);
    fillEmail(userRef);
}

void setStringHook(void* self, void* userRef, void* data, void* method) {
    setLocal(T_USER_SET_LOCAL, self, userRef, data, method);
}

void setArrayHook(void* self, void* userRef, void* data, void* method) {
    setLocal(T_USER_SET_LOCAL_ARRAY, self, userRef, data, method);
}

void setListHook(void* self, void* userRef, void* data, void* method) {
    setLocal(T_USER_SET_LOCAL_LIST, self, userRef, data, method);
}

bool resolvePrivacyFields() {
    void* manager = classOf("Net__Account__Manager");
    return resolveField(manager, "user_Select", userSelectField) &&
           resolveField(fieldClass(userSelectField), "uid", uidField) &&
           resolveField(fieldClass(userSelectField), "email", emailField);
}

} // namespace

void installPrivacyHooks() {
    if (resolvePrivacyFields())
        LOGI("Privacy fields OK");
    else
        LOGE("Privacy fields not found; a missing email stays blank");

    const TargetId setters[] = {T_USER_SET, T_USER_SET_LOCAL, T_USER_SET_LOCAL_ARRAY,
                                T_USER_SET_LOCAL_LIST};
    int found = 0;
    for (TargetId id : setters)
        found += target(id).addr ? 1 : 0;
    LOGI("Email setters: %d of 4", found);

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
    installHook(target(T_USER_SET), reinterpret_cast<void*>(setHook), "Manager.User__Set");
    installHook(target(T_USER_SET_LOCAL), reinterpret_cast<void*>(setStringHook),
                "Manager.User__Set__From_Local(string)");
    installHook(target(T_USER_SET_LOCAL_ARRAY), reinterpret_cast<void*>(setArrayHook),
                "Manager.User__Set__From_Local(string[])");
    installHook(target(T_USER_SET_LOCAL_LIST), reinterpret_cast<void*>(setListHook),
                "Manager.User__Set__From_Local(List)");
}
