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

// A user object and a guest account keep the email and the privacy value that
// decides whether it may be sent in the same shape, so one resolver serves both.
struct EmailFields {
    Field email;
    Field privacy;
    Field privacyEmail;
    Field visible;
};

EmailFields userEmail = {};
EmailFields guestEmail = {};
Field userSelectField = {};
Field guestAccountField = {};
bool fillLogged = false;

using SetFn = void (*)(void*, void*, void*, void*, bool, void*);
using SetLocalFn = void (*)(void*, void*, void*, void*);
using CommonFn = void (*)(void*, void*, bool, void*, void*, void*, void*, void*, void*);
using PrivacyFn = void (*)(void*, void*, int, void*, void*);

// The game's own word for an email privacy value, so the row reads like the
// setting screen instead of a word invented here.
void* privacyLabel(int visible) {
    auto fn = reinterpret_cast<void* (*)(int, void*)>(target(T_PRIVACY_LABEL).addr);
    return fn ? fn(visible, target(T_PRIVACY_LABEL).method) : nullptr;
}

// Another account's email only travels when its owner allows it, so a hidden
// one leaves the row blank. The owner's privacy label is shown instead.
void fillEmail(void* owner, const EmailFields& f) {
    if (!owner)
        return;
    void* email = readPtr(owner, f.email);
    if (email && stringLength(email) > 0)
        return;
    void* privacy = readPtr(owner, f.privacy);
    void* emailPrivacy = privacy ? readPtr(privacy, f.privacyEmail) : nullptr;
    if (!emailPrivacy)
        return;
    void* label = privacyLabel(readInt(emailPrivacy, f.visible));
    if (!label)
        return;
    setRef(owner, f.email, label);
    if (fillLogged)
        return;
    fillLogged = true;
    char text[64];
    if (toUtf8(label, text, sizeof(text)))
        LOGI("Email privacy label: %s", text);
}

bool resolveEmailFields(EmailFields& f, void* klass) {
    if (!klass)
        return false;
    return resolveField(klass, "email", f.email) &&
           resolveField(klass, "privacy", f.privacy) &&
           resolveField(fieldClass(f.privacy), "email", f.privacyEmail) &&
           resolveField(fieldClass(f.privacyEmail), "visible", f.visible);
}

void fillUserEmail(void* userRef) {
    fillEmail(userRef ? *reinterpret_cast<void**>(userRef) : nullptr, userEmail);
}

void setHook(void* self, void* userRef, void* uid, void* docs, bool overwrite, void* method) {
    auto orig = reinterpret_cast<SetFn>(target(T_USER_SET).orig);
    if (orig)
        orig(self, userRef, uid, docs, overwrite, method);
    fillUserEmail(userRef);
}

// The three local setters differ only by the type of their second argument.
void setLocal(TargetId id, void* self, void* userRef, void* data, void* method) {
    auto orig = reinterpret_cast<SetLocalFn>(target(id).orig);
    if (orig)
        orig(self, userRef, data, method);
    fillUserEmail(userRef);
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

// The profile packet builds the guest account and the privacy message may
// replace its email again, so both paths end in the same fill.
void commonHook(void* self, void* user, bool logined, void* uid, void* email, void* date,
                void* portrait, void* intro, void* method) {
    auto orig = reinterpret_cast<CommonFn>(target(T_GUEST_COMMON).orig);
    if (orig)
        orig(self, user, logined, uid, email, date, portrait, intro, method);
    fillEmail(readPtr(self, guestAccountField), guestEmail);
}

void privacyEmailHook(void* self, void* user, int visible, void* email, void* method) {
    auto orig = reinterpret_cast<PrivacyFn>(target(T_GUEST_PRIVACY_EMAIL).orig);
    if (orig)
        orig(self, user, visible, email, method);
    fillEmail(readPtr(self, guestAccountField), guestEmail);
}

bool resolvePrivacyFields() {
    void* manager = classOf("Net__Account__Manager");
    void* guest = classOf("Guest__Manager");
    return resolveField(manager, "user_Select", userSelectField) &&
           resolveEmailFields(userEmail, fieldClass(userSelectField)) &&
           resolveField(guest, "account", guestAccountField) &&
           resolveEmailFields(guestEmail, fieldClass(guestAccountField));
}

} // namespace

void installPrivacyHooks() {
    if (resolvePrivacyFields())
        LOGI("Privacy fields OK");
    else
        LOGE("Privacy fields not found; a hidden email stays blank");

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
    installHook(target(T_GUEST_COMMON), reinterpret_cast<void*>(commonHook),
                "Guest.Account__Common");
    installHook(target(T_GUEST_PRIVACY_EMAIL), reinterpret_cast<void*>(privacyEmailHook),
                "Guest.Account__Privacy__Email");
}
