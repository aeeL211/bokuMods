#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field inputLimit = {};
Field nameData = {};
Field nameLimit = {};

// The game rewrites UIInput.characterLimit (name 38, day 2, month 2, year 4)
// right before each edit, so both truncation points have to override it:
// Insert(string) enforces the limit per keystroke, Validate(string) trims the
// value on Set/submit.
void* validateHook(void* self, void* value, void* method) {
    auto orig = reinterpret_cast<void* (*)(void*, void*, void*)>(target(T_INPUT_VALIDATE).orig);
    if (!orig)
        return value;
    writeInt(self, inputLimit, INT32_MAX);
    return orig(self, value, method);
}

void insertHook(void* self, void* text, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_INPUT_INSERT).orig);
    if (!orig)
        return;
    writeInt(self, inputLimit, INT32_MAX);
    orig(self, text, method);
}

// Length is unused on the mobile path, but the Steam/gamepad path forwards it
// to its own text input. Zero means no character limit there.
void keyboardOpenHook(void* self, int32_t s, int32_t type, void* text, int32_t, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, int32_t, void*, int32_t, void*)>(
        target(T_KEYBOARD_OPEN).orig);
    if (!orig)
        return;
    // Names the string the engine is about to convert, so a bad pointer shows up
    // in the log right before the crash instead of only in the fault address.
    LOGI("Keyboard: s=%d type=%d text=%p", s, type, text);
    orig(self, s, type, text, 0, method);
}

// Name__Check trims the profile name to _name.length_Limit and falls back to a
// default when the result is empty. Raise the limit around the call so the trim
// passes and the rest of the check still runs.
void nameCheckHook(void* self, void** name, bool fix, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void**, bool, void*)>(target(T_NAME_CHECK).orig);
    if (!orig)
        return;
    void* data = readPtr(self, nameData);
    int32_t saved = 0;
    if (data) {
        saved = readInt(data, nameLimit);
        writeInt(data, nameLimit, INT32_MAX);
    }
    orig(self, name, fix, method);
    if (data)
        writeInt(data, nameLimit, saved);
}

} // namespace

void installTextInputHooks() {
    if (!resolveField(classOf("UIInput"), "characterLimit", inputLimit)) {
        LOGE("Field UIInput.characterLimit not found; input limits stay default");
    }

    void* accountManager = classOf("Net__Account__Manager");
    if (resolveField(accountManager, "_name", nameData) &&
        resolveField(fieldClass(nameData), "length_Limit", nameLimit)) {
        LOGI("Name limit field OK");
    } else {
        LOGE("Name limit field not found; profile name stays capped");
    }

    installHook(target(T_INPUT_VALIDATE), reinterpret_cast<void*>(validateHook),
                "UIInput.Validate");
    installHook(target(T_INPUT_INSERT), reinterpret_cast<void*>(insertHook), "UIInput.Insert");
    installHook(target(T_KEYBOARD_OPEN), reinterpret_cast<void*>(keyboardOpenHook),
                "UI__Manager.Keyboard__Open");
    installHook(target(T_NAME_CHECK), reinterpret_cast<void*>(nameCheckHook),
                "Account.Name__Check");
}
