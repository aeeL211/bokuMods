#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

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

    installHook(target(T_INTRODUCE_SET), reinterpret_cast<void*>(introduceHook),
                "Detail.Introduce__Set");
}
