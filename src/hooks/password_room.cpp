#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field detailField = {};
Field passwordField = {};
Field strField = {};
Field roomDataField = {};
Field roomPasswordField = {};

// Term__Bottom_Button() is the enabled state of the OK button: it reports
// whether _password.str differs from the empty string. Input__Ok() returns
// immediately when it is false, and the button itself is dimmed, so an empty
// box could never submit and the compare was never reached. The button is
// always usable with this hook; the room password is still checked by the
// original code, which okHook satisfies just before the compare.
bool buttonHook(void*, void*) { return true; }

// Input__Ok compares _password.str with detail._room_Data.password and only then
// closes the keyboard and calls Join__Start. That comparison is a local UI gate:
// Join__Start joins with _room_Data.key and never reads the password. The box
// stays empty, so copy the room's own password into _password.str before the
// compare. It then matches itself, the rest of the flow (keyboard close, cursor
// refresh, join) still runs, and the real password reaches the join path.
void okHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PASSWORD_OK).orig);
    if (!orig)
        return;
    void* detail = readPtr(self, detailField);
    void* roomData = readPtr(detail, roomDataField);
    void* roomPassword = readPtr(roomData, roomPasswordField);
    void* value = readPtr(self, passwordField);
    if (roomPassword && value && strField.field) {
        LOGI("Password: room password applied, len=%d", stringLength(roomPassword));
        setRef(value, strField, roomPassword);
    } else {
        LOGE("Password: room fields missing, compare runs as-is");
    }
    orig(self, method);
    // Reaching this line tells the log that the gated path completed, so a
    // crash right after is outside Input__Ok.
    LOGI("Password: ok returned");
}

// The keyboard path forwards _password.str as its initial text, so a null there
// aborts before the field opens. _Start normally writes an empty string; seed
// one when it is missing instead of writing the room password, which would show
// up in the box.
void fieldHook(void* self, bool touch, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PASSWORD_FIELD).orig);
    if (!orig)
        return;
    void* value = readPtr(self, passwordField);
    void* str = value ? readPtr(value, strField) : nullptr;
    // The panel hands this string to the keyboard, so log it before the original
    // runs: a null or stale pointer here is what the engine then chokes on.
    LOGI("Password field: touch=%d str=%p", touch ? 1 : 0, str);
    if (value && !str) {
        void* empty = newUtf16String(reinterpret_cast<const uint16_t*>(u""), 0);
        if (empty)
            setRef(value, strField, empty);
    }
    orig(self, touch, method);
}

} // namespace

void installPasswordHooks() {
    void* password = classOf("UI__Menu__Net__Multi__Join__Room_List__Detail__Password");
    if (password && resolveField(password, "detail", detailField) &&
        resolveField(password, "_password", passwordField) &&
        resolveField(fieldClass(passwordField), "str", strField) &&
        resolveField(fieldClass(detailField), "_room_Data", roomDataField) &&
        resolveField(fieldClass(roomDataField), "password", roomPasswordField)) {
        LOGI("Password room fields OK");
    } else {
        LOGE("Password room fields not found; password rooms stay manual");
    }

    installHook(target(T_PASSWORD_OK), reinterpret_cast<void*>(okHook), "Password.Input__Ok");
    installHook(target(T_PASSWORD_FIELD), reinterpret_cast<void*>(fieldHook),
                "Password.Input_Field__Input");
    installHook(target(T_PASSWORD_BUTTON), reinterpret_cast<void*>(buttonHook),
                "Password.Term__Bottom_Button");
}
