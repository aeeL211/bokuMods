#include "hooks.h"

#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// The throw entry scales its speed with this, so the top of the range is the
// strongest throw the piece can make.
constexpr float kFullPower = 1.0f;

// The stock throw charges while the button is down and only fires on release,
// so a held button needs a trigger of its own. The window matches the pistol
// and the buster repeats.
constexpr long long kThrowCadenceMs = 180;

Field managerField = {};
Field managerStateField = {};
Field managerThrowField = {};
Field chargeField = {};
Field chargeActiveField = {};

// Every player throws from its own state object, so the cadence, the last
// throw direction and the charge frames follow the player manager.
struct Throw {
    void* manager;
    long long lastMs;
    bool up;
    int chargeFrames;
};
constexpr int kThrowCount = 32;
Throw throws[kThrowCount] = {};
int nextThrow = 0;

long long nowMs() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

Throw& throwFor(void* manager) {
    Throw* freeSlot = nullptr;
    for (Throw& t : throws) {
        if (t.manager == manager)
            return t;
        if (!t.manager && !freeSlot)
            freeSlot = &t;
    }

    Throw& slot = freeSlot ? *freeSlot : throws[nextThrow];
    nextThrow = (nextThrow + 1) % kThrowCount;
    slot = {};
    slot.manager = manager;
    return slot;
}

// True at most once per window, so a held button throws at a fixed rate.
bool cadenceReady(Throw& t) {
    long long now = nowMs();
    if (now - t.lastMs < kThrowCadenceMs)
        return false;
    t.lastMs = now;
    return true;
}

// Records the direction of the throw that is starting and arms the cadence, so
// the first repeat waits one window instead of landing on the stock throw.
void markThrow(void* self, bool up) {
    void* manager = readPtr(self, managerField);
    if (!manager)
        return;
    Throw& t = throwFor(manager);
    t.up = up;
    t.lastMs = nowMs();
    t.chargeFrames = 0;
}

// The charge keeps the release handler armed, so it is dropped once the throw
// already left the hand and the release would add a second one.
void clearCharge(void* manager) {
    void* state = readPtr(manager, managerStateField);
    void* throwData = readPtr(state, managerThrowField);
    void* charge = readPtr(throwData, chargeField);
    if (charge)
        writeBool(charge, chargeActiveField, false);
}

// The standalone throw calls a single entry that takes the power, so the same
// entry is reused for the repeat.
void throwNow(void* self, bool up) {
    void* entry = target(up ? T_FECES_ACTION_UP : T_FECES_ACTION_STD).addr;
    if (entry)
        reinterpret_cast<void (*)(void*, float, void*)>(entry)(self, kFullPower, nullptr);
}

// The straight charge is entered from every aim, and the look-up one replaces it
// when the player aims up, so the last charge entry is the direction the held
// button has to repeat. Only a direction change re-arms the frame count, so a
// fresh charge gets its aim settled without holding a repeated entry back.
void setChargeDirection(void* self, bool up) {
    void* manager = readPtr(self, managerField);
    if (!manager)
        return;
    Throw& t = throwFor(manager);
    if (t.up != up)
        t.chargeFrames = 0;
    t.up = up;
}

void chargeStdHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_STD).orig);
    if (!orig)
        return;
    orig(self, method);
    setChargeDirection(self, false);
}

// The look-up charge has one caller, the input switch that runs while the
// player aims up during a charge.
void chargeUpHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_UP).orig);
    if (!orig)
        return;
    orig(self, method);
    setChargeDirection(self, true);
}

// The empty hand id at the end of the throw takes the piece out of the hands.
// Keeping that change back leaves the piece in place, so the same one can fly
// again, and the power is forced so every throw is the strongest.
void actionStdHook(void* self, float, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, float, void*)>(target(T_FECES_ACTION_STD).orig);
    if (!orig)
        return;
    markThrow(self, false);
    setKeepHand(true);
    orig(self, kFullPower, method);
    setKeepHand(false);
}

// The look-up throw needs the same treatment.
void actionUpHook(void* self, float, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, float, void*)>(target(T_FECES_ACTION_UP).orig);
    if (!orig)
        return;
    markThrow(self, true);
    setKeepHand(true);
    orig(self, kFullPower, method);
    setKeepHand(false);
}

// Runs every frame the throw action is on screen, and the stock path ends that
// action once the sprite reaches its last step. Starting the throw after it
// throws again while the button is held.
void throwUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_UPDATE).orig);
    if (!orig)
        return;
    orig(self, method);

    void* manager = readPtr(self, managerField);
    if (!manager || !actionHeld(manager))
        return;
    Throw& t = throwFor(manager);
    if (cadenceReady(t))
        throwNow(self, t.up);
}

// Runs every frame of the charge state. A held button throws at once instead of
// waiting for the release, so the first throw of a hold lands like every repeat.
// The aim switch runs on the next input pass, so the first charge frame only
// counts and the throw starts one frame later with the settled direction.
void chargeUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_UPDATE).orig);
    if (!orig)
        return;
    orig(self, method);

    void* manager = readPtr(self, managerField);
    if (!manager || !actionHeld(manager))
        return;
    Throw& t = throwFor(manager);
    if (t.chargeFrames == 0) {
        t.chargeFrames = 1;
        return;
    }
    if (!cadenceReady(t))
        return;
    clearCharge(manager);
    throwNow(self, t.up);
}

bool resolveFecesFields() {
    void* manager = classOf("Player__Manager");
    return resolveField(classOf("Player__State__Std__Action__Hh__Nature"), "m", managerField) &&
           resolveField(manager, "__state", managerStateField) &&
           resolveField(fieldClass(managerStateField), "_throw", managerThrowField) &&
           resolveField(fieldClass(managerThrowField), "charge", chargeField) &&
           resolveField(fieldClass(chargeField), "active", chargeActiveField);
}

} // namespace

void installFecesHooks() {
    if (!resolveFecesFields()) {
        LOGE("Feces fields not found; the throw stays stock");
        return;
    }
    LOGI("Feces fields OK");

    installHook(target(T_FECES_CHARGE_UPDATE), reinterpret_cast<void*>(chargeUpdateHook));
    installHook(target(T_FECES_CHARGE_STD), reinterpret_cast<void*>(chargeStdHook));
    installHook(target(T_FECES_CHARGE_UP), reinterpret_cast<void*>(chargeUpHook));
    installHook(target(T_FECES_UPDATE), reinterpret_cast<void*>(throwUpdateHook));
    installHook(target(T_FECES_ACTION_STD), reinterpret_cast<void*>(actionStdHook));
    installHook(target(T_FECES_ACTION_UP), reinterpret_cast<void*>(actionUpHook));
}
