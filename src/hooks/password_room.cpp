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

// The room data owns the room password, so reading it on demand keeps every
// user of the value on the same live object.
void* roomPassword(void* self) {
    void* detail = readPtr(self, detailField);
    void* roomData = readPtr(detail, roomDataField);
    return readPtr(roomData, roomPasswordField);
}

// Term__Bottom_Button() is the enabled state of the OK button: it reports
// whether _password.str differs from the empty string. Input__Ok() returns
// immediately when it is false, and the button itself is dimmed, so an empty
// box could never submit and the compare was never reached. The button is
// always usable with this hook; the room password is still checked by the
// original code, which okHook satisfies just before the compare.
bool buttonHook(void*, void*) { return true; }

// Input__Ok compares _password.str with detail._room_Data.password and only then
// closes the keyboard and calls Join__Start. The box stays empty, so the room's
// own string object is put in place for that compare: String.Equals returns true
// on the same reference before it reads a character, and the room data keeps the
// object alive.
void okHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PASSWORD_OK).orig);
    if (!orig)
        return;
    void* value = readPtr(self, passwordField);
    void* password = roomPassword(self);
    if (value && usableString(password)) {
        setRef(value, strField, password);
        // The compare reads this field after the original runs, so a write that
        // did not land is the one failure the next log must make obvious.
        void* stored = readPtr(value, strField);
        if (stored == password)
            LOGI("Password: room password set, len=%d", stringLength(password));
        else
            LOGE("Password: room password write lost, stored=%p want=%p", stored, password);
    } else {
        LOGE("Password: room password missing, compare runs as-is");
    }
    // Reaching this line means the compare failed: the passing path tail-calls
    // Join__Start and never comes back.
    orig(self, method);
    LOGI("Password: gate compare failed, ok returned");
}

// The keyboard path forwards _password.str as its initial text, so a null there
// aborts before the field opens. _Start normally writes an empty string; after a
// join the panel can still hold a recycled one instead. The room password is put
// back for that case, because it is owned by the room data and stays alive while
// the keyboard reads it, while a fresh allocation can be collected too early.
void fieldHook(void* self, bool touch, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PASSWORD_FIELD).orig);
    if (!orig)
        return;
    void* value = readPtr(self, passwordField);
    void* str = value ? readPtr(value, strField) : nullptr;
    // The panel hands this string to the keyboard, so log it before the original
    // runs: a stale pointer here is what the engine then chokes on.
    LOGI("Password field: value=%p str=%p len=%d", value, str, stringLength(str));
    if (value && !usableString(str)) {
        void* password = roomPassword(self);
        void* seed = usableString(password) ? password : emptyString();
        if (seed)
            setRef(value, strField, seed);
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
