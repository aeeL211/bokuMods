#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field weaponManagerField = {};
Field cmnManagerField = {};
Field cmnHhField = {};
Field stateField = {};
Field comboField = {};
Field rockField = {};
Field chargeField = {};
// The runtime charge counter is the 32-bit step count the blaster id lookup
// compares with the configured full step.
Field stepField = {};
Field inputField = {};
Field inputManagerField = {};
Field actionField = {};
Field keepField = {};
Field hhRockField = {};
Field configChargeField = {};
Field fullStepField = {};

void* playerState(void* self, const Field& managerField) {
    void* manager = readPtr(self, managerField);
    return readPtr(manager, stateField);
}

// The action and end update repeat the shot only while the state's combo flag
// is set. The flag follows the interaction button, so holding it down fires
// again every time the stock timing allows.
void syncCombo(void* self, const Field& managerField) {
    void* manager = readPtr(self, managerField);
    void* input = readPtr(manager, inputField);
    void* inputManager = readPtr(input, inputManagerField);
    void* action = readPtr(inputManager, actionField);
    void* state = readPtr(manager, stateField);
    if (state && action)
        writeBool(state, comboField, readBool(action, keepField));
}

// The gun end state is a fixed timer. Ending it on the first update removes the
// pause between shots; the action phase still plays the muzzle and bullet.
bool gunEndHook(void*, void*) { return true; }

void gunUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_GUN_UPDATE).orig);
    if (!orig)
        return;

    syncCombo(self, weaponManagerField);
    orig(self, method);
}

void blasterUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_UPDATE).orig);
    if (!orig)
        return;

    syncCombo(self, weaponManagerField);
    orig(self, method);
}

// The blaster end state waits out the shot interval before it starts the next
// action. With the combo flag set from the held button, the stock update starts
// the next shot on its own as soon as the interval elapses.
void blasterEndUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_END_UPDATE).orig);
    if (!orig)
        return;

    syncCombo(self, weaponManagerField);
    orig(self, method);
}

// The id lookup returns the charged projectile only when the runtime charge
// step equals the configured full step. Writing that step before the lookup
// makes every shot use the strongest ammo.
int32_t blasterIdHook(void* self, void* method) {
    auto orig = reinterpret_cast<int32_t (*)(void*, void*)>(target(T_ROCK_ID).orig);
    if (!orig)
        return 0;

    void* state = playerState(self, cmnManagerField);
    void* rock = readPtr(state, rockField);
    void* charge = readPtr(rock, chargeField);
    void* hh = readPtr(self, cmnHhField);
    void* config = readPtr(hh, hhRockField);
    void* configCharge = readPtr(config, configChargeField);
    if (charge && configCharge)
        writeInt(charge, stepField, readInt(configCharge, fullStepField));

    return orig(self, method);
}

bool resolveWeaponFields() {
    void* cmn = classOf("Player__State__Cmn");
    void* weapon = classOf("Player__State__Std__Action__Hh__Weapon");
    return resolveField(weapon, "m", weaponManagerField) &&
           resolveField(cmn, "m", cmnManagerField) && resolveField(cmn, "chr_Hh", cmnHhField) &&
           resolveField(fieldClass(weaponManagerField), "__state", stateField) &&
           resolveField(fieldClass(stateField), "combo", comboField) &&
           resolveField(fieldClass(stateField), "_rockman__Rock_Buster", rockField) &&
           resolveField(fieldClass(rockField), "charge", chargeField) &&
           resolveField(fieldClass(chargeField), "step", stepField) &&
           resolveField(fieldClass(weaponManagerField), "input", inputField) &&
           resolveField(fieldClass(inputField), "input", inputManagerField) &&
           resolveField(fieldClass(inputManagerField), "action", actionField) &&
           resolveField(fieldClass(actionField), "keep", keepField) &&
           resolveField(fieldClass(cmnHhField), "_rockman__Rock_Buster", hhRockField) &&
           resolveField(fieldClass(hhRockField), "charge", configChargeField) &&
           resolveField(fieldClass(configChargeField), "step__Full", fullStepField);
}

} // namespace

void installWeaponHooks() {
    if (!resolveWeaponFields()) {
        LOGE("Weapon fields not found; weapons stay stock");
        return;
    }
    LOGI("Weapon fields OK");

    installHook(target(T_GUN_END), reinterpret_cast<void*>(gunEndHook),
                "Cmn.Term__Gun_Shoot__End__Update__End");
    installHook(target(T_GUN_UPDATE), reinterpret_cast<void*>(gunUpdateHook),
                "Weapon.Gun_Shoot__Action__Update");
    installHook(target(T_ROCK_UPDATE), reinterpret_cast<void*>(blasterUpdateHook),
                "Weapon.Rockman__Rock_Buster__Action__Update");
    installHook(target(T_ROCK_END_UPDATE), reinterpret_cast<void*>(blasterEndUpdateHook),
                "Weapon.Rockman__Rock_Buster__End__Update");
    installHook(target(T_ROCK_ID), reinterpret_cast<void*>(blasterIdHook),
                "Cmn.Rockman__Rock_Buster__Id");
}
