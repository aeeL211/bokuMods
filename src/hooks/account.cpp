#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

using DateYmd = void (*)(void*, int32_t, int32_t, int32_t, int32_t, void*);

bool dateMissingLogged = false;

// The account row passes the register date as text. The numeric overload is
// called instead, so the row always shows 11 February 2112.
void dateHook(void* self, int32_t index, void* text, void* method) {
    auto numeric = reinterpret_cast<DateYmd>(target(T_REGISTER_DATE_YMD).addr);
    if (numeric) {
        numeric(self, index, 2112, 2, 11, target(T_REGISTER_DATE_YMD).method);
        return;
    }

    // Without the numeric overload the account's own date is the only safe
    // value, so fall back to the text row instead of leaving it blank.
    if (!dateMissingLogged) {
        LOGE("Register date: numeric overload not found; keeps the account value");
        dateMissingLogged = true;
    }
    auto orig =
        reinterpret_cast<void (*)(void*, int32_t, void*, void*)>(target(T_REGISTER_DATE).orig);
    if (orig)
        orig(self, index, text, method);
}

// The local email check is a format regex. Accepting every string lets login,
// sign-up and change-email pass their own guard before the backend sees it.
bool emailHook(void*, void*, void*) { return true; }

Field accountField = {};
Field introduceField = {};
Field lengthField = {};

// Introduce__Set truncates the text to _introduce.length before storing it.
// Raising that limit keeps the whole text.
void introduceHook(void* self, void* text, void* method) {
    if (accountField.field && introduceField.field && lengthField.field) {
        void* account = readPtr(self, accountField);
        void* introduce = readPtr(account, introduceField);
        if (introduce)
            writeInt(introduce, lengthField, INT32_MAX);
    }

    auto orig = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_INTRODUCE_SET).orig);
    if (orig)
        orig(self, text, method);
}

bool resolveAccountFields() {
    return resolveField(classOf("Net__Account__My_Account__Detail"), "account", accountField) &&
           resolveField(fieldClass(accountField), "_introduce", introduceField) &&
           resolveField(fieldClass(introduceField), "length", lengthField);
}

} // namespace

void installAccountHooks() {
    if (resolveAccountFields()) {
        LOGI("Account fields OK");
    } else {
        LOGE("Account fields not found; introduce stays capped");
    }

    installHook(target(T_REGISTER_DATE), reinterpret_cast<void*>(dateHook),
                "Manager.User_Detail__Content__Date");
    installHook(target(T_EMAIL_FORMAT), reinterpret_cast<void*>(emailHook),
                "Manager.Term__Email__Format");
    installHook(target(T_INTRODUCE_SET), reinterpret_cast<void*>(introduceHook),
                "Detail.Introduce__Set");
}
