#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Just under the configured interval, so the next update crosses it.
constexpr float kIntervalEpsilon = 0.0001f;

// The blaster's configured full charge step. The id lookup picks the charged
// projectile only when the charge counter equals this value.
constexpr int32_t kFullChargeStep = 3;

Field cmnManagerField = {};
Field weaponManagerField = {};
Field stateField = {};
Field stateTimerField = {};
Field rockField = {};
Field intervalField = {};
Field intervalTimerField = {};
Field chargeField = {};
// The charge counter lives in the field the dump calls "active", but the game
// reads, increments and compares it as a 32-bit step count.
Field stepField = {};
Field comboField = {};
Field weaponHhField = {};
Field hhRockField = {};
Field shootField = {};
Field shootTimeField = {};
Field shootEndField = {};
Field shootIntervalField = {};

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
// combo flag starts the next shot at once while the button stays down. This is
// the path used by every stance except the weapon one below.
bool blasterEndToActionHook(void* self, void* method) {
    void* state = playerState(self, cmnManagerField);
    if (state && comboField.field)
        return readInt(state, comboField) != 0;

    // Without the state fields the stock interval is the only safe answer.
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_END_TO_ACTION).orig);
    return orig && orig(self, method);
}

// The weapon class runs its own end update instead of End__To__Action. With the
// button held, the interval timer is moved just under its configured duration so
// the stock update starts the next shot on this frame. With the button released,
// the state timer is moved the same way, so the end tail is skipped at once.
void blasterEndUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_END_UPDATE).orig);
    if (!orig)
        return;

    void* state = playerState(self, weaponManagerField);
    void* hh = readPtr(self, weaponHhField);
    void* config = readPtr(hh, hhRockField);
    void* shoot = readPtr(config, shootField);
    void* time = readPtr(shoot, shootTimeField);
    if (state && time) {
        if (readInt(state, comboField) != 0) {
            void* rock = readPtr(state, rockField);
            void* interval = readPtr(rock, intervalField);
            if (interval)
                writeFloat(interval, intervalTimerField,
                           readFloat(time, shootIntervalField) - kIntervalEpsilon);
        } else {
            writeFloat(state, stateTimerField, readFloat(time, shootEndField) - kIntervalEpsilon);
        }
    }

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
        !resolveField(weapon, "chr_Hh", weaponHhField) ||
        !resolveField(fieldClass(weaponManagerField), "__state", stateField) ||
        !resolveField(fieldClass(stateField), "timer", stateTimerField) ||
        !resolveField(fieldClass(stateField), "combo", comboField) ||
        !resolveField(fieldClass(stateField), "_rockman__Rock_Buster", rockField) ||
        !resolveField(fieldClass(rockField), "interval", intervalField) ||
        !resolveField(fieldClass(intervalField), "timer", intervalTimerField) ||
        !resolveField(fieldClass(rockField), "charge", chargeField) ||
        !resolveField(fieldClass(chargeField), "active", stepField) ||
        !resolveField(fieldClass(weaponHhField), "_rockman__Rock_Buster", hhRockField) ||
        !resolveField(fieldClass(hhRockField), "shoot", shootField) ||
        !resolveField(fieldClass(shootField), "_time", shootTimeField) ||
        !resolveField(fieldClass(shootTimeField), "end", shootEndField) ||
        !resolveField(fieldClass(shootTimeField), "interval", shootIntervalField))
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
