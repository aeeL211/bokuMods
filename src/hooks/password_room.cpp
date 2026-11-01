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

// A room password is a few characters. Anything past this is a stale object
// whose header was reused, not a real string, and passing it to the keyboard
// makes Unity scan past the end of the heap.
constexpr int32_t kMaxPasswordLen = 4096;

bool usableString(void* str) {
    if (!str)
        return false;
    int32_t len = stringLength(str);
    return len >= 0 && len <= kMaxPasswordLen;
}

void* emptyString() { return newUtf16String(reinterpret_cast<const uint16_t*>(u""), 0); }

// Copies a managed string into a fresh object. The copy is what keeps the value
// alive; the room data that owns the original is torn down by the join.
void* copyString(void* str) {
    if (!usableString(str))
        return nullptr;
    return newUtf16String(stringChars(str), stringLength(str));
}

// Term__Bottom_Button() is the enabled state of the OK button: it reports
// whether _password.str differs from the empty string. Input__Ok() returns
// immediately when it is false, and the button itself is dimmed, so an empty
// box could never submit and the compare was never reached. The button is
// always usable with this hook; the room password is still checked by the
// original code, which okHook satisfies just before the compare.
bool buttonHook(void*, void*) { return true; }

// Input__Ok compares _password.str with detail._room_Data.password and only then
// closes the keyboard and calls Join__Start. The box stays empty, so a fresh
// copy of the room's own password is put in place for that compare and for the
// join request it builds. Nothing is written back afterwards: the previous
// value can be owned by the room data, and Join__Start tears that data down, so
// restoring it leaves the panel holding a string whose memory is recycled. The
// next keyboard open then reads a bogus length from it and the engine scans off
// the heap.
void okHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PASSWORD_OK).orig);
    if (!orig)
        return;
    void* detail = readPtr(self, detailField);
    void* roomData = readPtr(detail, roomDataField);
    void* roomPassword = readPtr(roomData, roomPasswordField);
    void* value = readPtr(self, passwordField);
    void* previous = value ? readPtr(value, strField) : nullptr;
    void* copy = copyString(roomPassword);
    if (value && copy) {
        LOGI("Password: room password applied, len=%d previous=%d", stringLength(copy),
             stringLength(previous));
        setRef(value, strField, copy);
    } else {
        LOGE("Password: room fields missing, compare runs as-is");
    }
    orig(self, method);
    // Reaching this line tells the log that the gated path completed, so a
    // crash right after is outside Input__Ok.
    LOGI("Password: ok returned, field len=%d", stringLength(readPtr(value, strField)));
}

// The keyboard path forwards _password.str as its initial text, so a null there
// aborts before the field opens. _Start normally writes an empty string; seed
// one when it is missing, and replace a value whose header no longer reads as a
// short string, which is what the engine chokes on.
void fieldHook(void* self, bool touch, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PASSWORD_FIELD).orig);
    if (!orig)
        return;
    void* value = readPtr(self, passwordField);
    void* str = value ? readPtr(value, strField) : nullptr;
    // The panel hands this string to the keyboard, so log it before the original
    // runs: a stale pointer here is what the engine then chokes on.
    LOGI("Password field: self=%p value=%p str=%p len=%d", self, value, str, stringLength(str));
    if (value && !usableString(str)) {
        void* empty = emptyString();
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
        LOGI("Password offsets: _password=%zu str=%zu room_Data=%zu room_password=%zu",
             passwordField.offset, strField.offset, roomDataField.offset, roomPasswordField.offset);
    } else {
        LOGE("Password room fields not found; password rooms stay manual");
    }

    installHook(target(T_PASSWORD_OK), reinterpret_cast<void*>(okHook), "Password.Input__Ok");
    installHook(target(T_PASSWORD_FIELD), reinterpret_cast<void*>(fieldHook),
                "Password.Input_Field__Input");
    installHook(target(T_PASSWORD_BUTTON), reinterpret_cast<void*>(buttonHook),
                "Password.Term__Bottom_Button");
}
