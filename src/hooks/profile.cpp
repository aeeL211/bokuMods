#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "log.h"
#include "targets.h"

namespace {

// Day and month fields open with 2 characters, year with 4.
int32_t birthdayLengthHook(void*, int32_t, void*) { return INT32_MAX; }

// Birthday__Refresh_ only shows a day/year when V <= Limit. The month keeps its
// 12 limit; day and year are opened up.
void birthdayHook(void* self, int32_t v, int32_t i, int32_t limit, void* def, void* method) {
    LOGI("Birthday: v=%d i=%d limit=%d", v, i, limit);
    if (limit == 31 || limit == 9999)
        limit = INT32_MAX;
    auto orig = reinterpret_cast<void (*)(void*, int32_t, int32_t, int32_t, void*, void*)>(
        target(T_BIRTHDAY_REFRESH).orig);
    if (!orig)
        return;
    orig(self, v, i, limit, def, method);
}

// Date_Check(ref Year, ref Month, ref Day) zeroes a year above 9999 and resets
// a day that does not exist in that month, which is what capped the birthday
// fields. Skip it only while the birthday field itself is being submitted, so
// every other caller keeps the stock date validation.
thread_local bool birthdayEdit = false;

void birthdayEndHook(void* self, void* text, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_BIRTHDAY_END).orig);
    if (!orig)
        return;
    birthdayEdit = true;
    orig(self, text, method);
    birthdayEdit = false;
}

void dateCheckHook(int32_t* year, int32_t* month, int32_t* day, void* method) {
    if (birthdayEdit) {
        // Keep values the rest of the game cannot index with, but leave the day
        // alone otherwise: 31 February is the point of this bypass.
        LOGI("Birthday: check skipped y=%d m=%d d=%d", year ? *year : -1, month ? *month : -1,
             day ? *day : -1);
        if (month && (*month < 0 || *month > 12))
            *month = 0;
        if (day && (*day < 0 || *day > 31))
            *day = 0;
        return;
    }
    auto orig =
        reinterpret_cast<void (*)(int32_t*, int32_t*, int32_t*, void*)>(target(T_DATE_CHECK).orig);
    if (!orig)
        return;
    orig(year, month, day, method);
}

} // namespace

void installProfileHooks() {
    installHook(target(T_BIRTHDAY_REFRESH), reinterpret_cast<void*>(birthdayHook),
                "Profile.Birthday__Refresh_");
    installHook(target(T_BIRTHDAY_LENGTH), reinterpret_cast<void*>(birthdayLengthHook),
                "Profile.Birthday__Length");
    installHook(target(T_BIRTHDAY_END), reinterpret_cast<void*>(birthdayEndHook),
                "Profile.Input_Field__End__Birthday");
    installHook(target(T_DATE_CHECK), reinterpret_cast<void*>(dateCheckHook),
                "MyBehaviour.Date_Check");
}
