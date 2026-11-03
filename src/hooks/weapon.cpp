#include "hooks.h"

#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field weaponManagerField = {};
Field weaponCmnField = {};
Field stateManagerField = {};
Field cmnManagerField = {};
Field cmnHhField = {};
Field stateField = {};
Field comboField = {};
Field rockField = {};
Field chargeField = {};
// The runtime charge counter is the 32-bit step count the buster id lookup
// compares with the configured full step.
Field stepField = {};
Field inputField = {};
Field inputManagerField = {};
Field actionField = {};
Field keepField = {};
Field hhRockField = {};
Field configChargeField = {};
Field fullStepField = {};

// A held button fires once per window. The stock gun and buster both wait out
// a long action timer, and the buster can burn its shot interval before the
// end state starts, after which the held button stops firing.
constexpr long long kGunCadenceMs = 80;
constexpr long long kBusterActionMs = 60;
constexpr long long kBusterCadenceMs = 100;

// Every player runs the same state machine, so the cadence follows the player
// manager. One shared clock would let a guest reset the local button edge.
struct Cadence {
    void* manager;
    long long gunShotMs;
    long long busterShotMs;
    bool held;
};
constexpr int kCadenceCount = 32;
Cadence cadences[kCadenceCount] = {};
int nextCadence = 0;

long long nowMs() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

Cadence& cadenceFor(void* manager) {
    Cadence* freeSlot = nullptr;
    for (Cadence& c : cadences) {
        if (c.manager == manager)
            return c;
        if (!c.manager && !freeSlot)
            freeSlot = &c;
    }

    Cadence& slot = freeSlot ? *freeSlot : cadences[nextCadence];
    nextCadence = (nextCadence + 1) % kCadenceCount;
    slot = {};
    slot.manager = manager;
    return slot;
}

// True at most once per window, so a held button fires at a fixed rate.
bool cadenceReady(long long& lastMs, long long cadenceMs) {
    long long now = nowMs();
    if (now - lastMs < cadenceMs)
        return false;
    lastMs = now;
    return true;
}

void* playerState(void* self, const Field& managerField) {
    void* manager = readPtr(self, managerField);
    return readPtr(manager, stateField);
}

void* actionOf(void* manager) {
    void* input = readPtr(manager, inputField);
    void* inputManager = readPtr(input, inputManagerField);
    return readPtr(inputManager, actionField);
}

// The interaction button is held when the action input keeps it.
bool buttonHeld(void* manager) {
    void* action = actionOf(manager);
    return action && readBool(action, keepField);
}

// The state update repeats a shot only while its combo flag is set, and the
// flag follows the held button. Writing it before the stock update lets every
// stance repeat, standing or moving.
void syncCombo(void* manager) {
    void* state = readPtr(manager, stateField);
    if (state)
        writeBool(state, comboField, buttonHeld(manager));
}

// The button press starts the stock shot, so the cadence counts from here and
// the first forced repeat lands one window later instead of firing at once.
void stateUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_STATE_UPDATE).orig);
    if (!orig)
        return;

    void* manager = readPtr(self, stateManagerField);
    Cadence& cadence = cadenceFor(manager);
    bool held = buttonHeld(manager);
    if (held && !cadence.held) {
        cadence.gunShotMs = nowMs();
        cadence.busterShotMs = cadence.gunShotMs;
    }
    cadence.held = held;
    orig(self, method);
}

// The gun end state is a fixed timer. Ending it on the first update removes the
// pause between shots; the action phase still plays the muzzle and bullet.
bool gunEndHook(void*, void*) { return true; }

// The stock gun action ends only after the second frame plus the action timer.
// Answering yes on a short cadence while the button is held fires again before
// that timer would let the stock path through.
bool gunActionEndHook(void* self, int img, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, int, void*)>(target(T_GUN_ACTION_END).orig);
    if (!orig)
        return false;

    void* manager = readPtr(self, cmnManagerField);
    Cadence& cadence = cadenceFor(manager);
    if (orig(self, img, method)) {
        if (buttonHeld(manager))
            cadence.gunShotMs = nowMs();
        return true;
    }
    if (!buttonHeld(manager))
        return false;
    return cadenceReady(cadence.gunShotMs, kGunCadenceMs);
}

// Keep the combo flag current during the gun action, then run the stock update.
void gunUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_GUN_UPDATE).orig);
    if (!orig)
        return;

    syncCombo(readPtr(self, weaponManagerField));
    orig(self, method);
}

// The same sync during the buster action.
void busterUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_UPDATE).orig);
    if (!orig)
        return;

    syncCombo(readPtr(self, weaponManagerField));
    orig(self, method);
}

// Every stance ends the buster action through this check, which waits out a
// long action timer. Ending it one action window after the shot starts keeps
// the muzzle and effect frames while removing the rest of the wait.
bool busterActionEndHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_ACTION_END).orig);
    if (!orig)
        return false;

    void* manager = readPtr(self, cmnManagerField);
    Cadence& cadence = cadenceFor(manager);
    if (orig(self, method))
        return true;
    if (!buttonHeld(manager))
        return false;
    return nowMs() - cadence.busterShotMs >= kBusterActionMs;
}

// The moving stances leave the buster end state through this check, which
// gives up once the shot interval passed. Answering yes on the cadence keeps
// the shot going for as long as the button is held.
bool busterEndToActionHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_END_TO_ACTION).orig);
    if (!orig)
        return false;

    void* manager = readPtr(self, cmnManagerField);
    Cadence& cadence = cadenceFor(manager);
    if (orig(self, method)) {
        if (buttonHeld(manager))
            cadence.busterShotMs = nowMs();
        return true;
    }
    if (!buttonHeld(manager))
        return false;
    return cadenceReady(cadence.busterShotMs, kBusterCadenceMs);
}

// The standing buster refires inside its own end update. Holding that update
// until the cadence is due and then starting the action keeps the same rate,
// and the stock path still runs whenever the button is released.
void busterEndUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_END_UPDATE).orig);
    if (!orig)
        return;

    auto endDir = reinterpret_cast<int32_t (*)(void*, void*)>(target(T_ROCK_END_DIR).addr);
    auto action = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_ROCK_ACTION).addr);
    void* manager = readPtr(self, weaponManagerField);
    void* cmn = readPtr(self, weaponCmnField);
    if (!endDir || !action || !cmn || !buttonHeld(manager)) {
        orig(self, method);
        return;
    }

    Cadence& cadence = cadenceFor(manager);
    if (!cadenceReady(cadence.busterShotMs, kBusterCadenceMs))
        return;

    action(self, endDir(cmn, nullptr), nullptr);
}

// The id lookup returns the charged projectile only when the runtime charge
// step equals the configured full step. Writing that step before the lookup
// makes every shot use the strongest ammo.
int32_t busterIdHook(void* self, void* method) {
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
    void* stateManager = classOf("Player__State");
    return resolveField(stateManager, "m", stateManagerField) &&
           resolveField(weapon, "m", weaponManagerField) &&
           resolveField(weapon, "cmn", weaponCmnField) && resolveField(cmn, "m", cmnManagerField) &&
           resolveField(cmn, "chr_Hh", cmnHhField) &&
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

    if (!target(T_ROCK_ACTION).addr || !target(T_ROCK_END_DIR).addr)
        LOGE("Buster refire methods not found; standing buster stays stock");

    installHook(target(T_STATE_UPDATE), reinterpret_cast<void*>(stateUpdateHook),
                "Player__State._Update");
    installHook(target(T_GUN_END), reinterpret_cast<void*>(gunEndHook),
                "Cmn.Term__Gun_Shoot__End__Update__End");
    installHook(target(T_GUN_ACTION_END), reinterpret_cast<void*>(gunActionEndHook),
                "Cmn.Term__Gun_Shoot__Action__Update__End");
    installHook(target(T_GUN_UPDATE), reinterpret_cast<void*>(gunUpdateHook),
                "Weapon.Gun_Shoot__Action__Update");
    installHook(target(T_ROCK_UPDATE), reinterpret_cast<void*>(busterUpdateHook),
                "Weapon.Rockman__Rock_Buster__Action__Update");
    installHook(target(T_ROCK_ACTION_END), reinterpret_cast<void*>(busterActionEndHook),
                "Cmn.Term__Rockman__Rock_Buster__Action__Update__End");
    installHook(target(T_ROCK_END_UPDATE), reinterpret_cast<void*>(busterEndUpdateHook),
                "Weapon.Rockman__Rock_Buster__End__Update");
    installHook(target(T_ROCK_END_TO_ACTION), reinterpret_cast<void*>(busterEndToActionHook),
                "Cmn.Term__Rockman__Rock_Buster__End__To__Action");
    installHook(target(T_ROCK_ID), reinterpret_cast<void*>(busterIdHook),
                "Cmn.Rockman__Rock_Buster__Id");
}
