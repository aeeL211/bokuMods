#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// A shared-state timer far beyond its configured tail, so Term_Timer reports
// the wait as done on the next update.
constexpr float kNoWait = 1.0e9f;

// The blaster's configured full charge step. The id lookup picks the charged
// projectile only when the charge step equals this value.
constexpr int32_t kFullChargeStep = 3;

Field cmnManagerField = {};
Field weaponManagerField = {};
Field stateField = {};
Field stateTimerField = {};
Field rockField = {};
Field chargeField = {};
Field stepField = {};
Field comboField = {};

void* playerState(void* self, const Field& managerField) {
    void* manager = readPtr(self, managerField);
    return readPtr(manager, stateField);
}

// The gun end state is a fixed timer. Ending it on the first update removes the
// pause between shots; the action phase still plays the muzzle and bullet.
bool gunEndHook(void*, void*) { return true; }

// The blaster action waits on its own animation before it reaches the end
// state. Ending it right away lets the next shot start without that tail.
bool blasterActionEndHook(void*, void*) { return true; }

// The end state normally waits out the shot interval. Answering with the held
// combo flag starts the next shot at once while the button stays down.
bool blasterEndToActionHook(void* self, void* method) {
    void* state = playerState(self, cmnManagerField);
    if (state && comboField.field)
        return readInt(state, comboField) != 0;

    // Without the state fields the stock interval is the only safe answer.
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_END_TO_ACTION).orig);
    return orig && orig(self, method);
}

// Once the button is released, the end state plays a short tail through the
// shared state timer. Marking it overdue drops that tail; a new press is handled
// by the end-to-action hook instead.
void blasterEndUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_END_UPDATE).orig);
    if (!orig)
        return;

    void* state = playerState(self, weaponManagerField);
    if (state && readInt(state, comboField) == 0)
        writeFloat(state, stateTimerField, kNoWait);

    orig(self, method);
}

// The blaster picks its projectile from the charge step. Forcing the full step
// before the lookup makes every shot use the strongest ammo.
int32_t blasterIdHook(void* self, void* method) {
    auto orig = reinterpret_cast<int32_t (*)(void*, void*)>(target(T_ROCK_ID).orig);
    if (!orig)
        return 0;

    void* state = playerState(self, cmnManagerField);
    void* blaster = readPtr(state, rockField);
    void* charge = readPtr(blaster, chargeField);
    if (charge)
        writeInt(charge, stepField, kFullChargeStep);

    return orig(self, method);
}

bool resolveWeaponFields() {
    void* cmn = classOf("Player__State__Cmn");
    void* weapon = classOf("Player__State__Std__Action__Hh__Weapon");
    if (!resolveField(cmn, "m", cmnManagerField) ||
        !resolveField(weapon, "m", weaponManagerField) ||
        !resolveField(fieldClass(weaponManagerField), "__state", stateField) ||
        !resolveField(fieldClass(stateField), "timer", stateTimerField) ||
        !resolveField(fieldClass(stateField), "combo", comboField) ||
        !resolveField(fieldClass(stateField), "_rockman__Rock_Buster", rockField) ||
        !resolveField(fieldClass(rockField), "charge", chargeField) ||
        !resolveField(fieldClass(chargeField), "step", stepField))
        return false;
    return true;
}

} // namespace

void installWeaponHooks() {
    if (resolveWeaponFields()) {
        LOGI("Weapon fields OK");
    } else {
        LOGE("Weapon fields not found; blaster stays stock");
    }

    installHook(target(T_GUN_END), reinterpret_cast<void*>(gunEndHook),
                "Cmn.Term__Gun_Shoot__End__Update__End");
    installHook(target(T_ROCK_ACTION_END), reinterpret_cast<void*>(blasterActionEndHook),
                "Cmn.Term__Rockman__Rock_Buster__Action__Update__End");
    installHook(target(T_ROCK_END_TO_ACTION), reinterpret_cast<void*>(blasterEndToActionHook),
                "Cmn.Term__Rockman__Rock_Buster__End__To__Action");
    installHook(target(T_ROCK_END_UPDATE), reinterpret_cast<void*>(blasterEndUpdateHook),
                "Weapon.Rockman__Rock_Buster__End__Update");
    installHook(target(T_ROCK_ID), reinterpret_cast<void*>(blasterIdHook),
                "Cmn.Rockman__Rock_Buster__Id");
}
