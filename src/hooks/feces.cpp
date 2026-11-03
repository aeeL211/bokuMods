#include "hooks.h"

#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// The throw animation keeps its state alive for a few frames, so the held
// button throws again as soon as this window is over.
constexpr long long kThrowCadenceMs = 180;

Field managerField = {};

struct Throw {
    void* manager;
    int32_t state;
    long long lastMs;
};
constexpr int kThrowCount = 32;
Throw throws[kThrowCount] = {};
int nextThrow = 0;

long long nowMs() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

// Every player throws from its own state object, so the cadence follows the
// manager the same way the weapon cadences do.
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

// The throw spawns its projectile and then asks for the empty hand id, which
// takes the piece out of the player's hands. Keeping that hand change back
// leaves the held piece in place, so the same piece can be thrown again.
void throwActionHook(void* self, int32_t throwState, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_FECES_THROW).orig);
    if (!orig)
        return;
    Throw& t = throwFor(readPtr(self, managerField));
    t.state = throwState;
    t.lastMs = nowMs();
    setKeepHand(true);
    orig(self, throwState, method);
    setKeepHand(false);
}

// This update runs every frame the throw is on screen. Starting the action
// again once the window passed throws the same piece again while the button is
// held, the same way the pistol and buster repeat.
void throwUpdateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_UPDATE).orig);
    if (!orig)
        return;
    orig(self, method);

    void* manager = readPtr(self, managerField);
    if (!manager || !actionHeld(manager))
        return;
    Throw& t = throwFor(manager);
    if (nowMs() - t.lastMs < kThrowCadenceMs)
        return;
    t.lastMs = nowMs();

    auto action = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_FECES_THROW).addr);
    if (action)
        action(self, t.state, nullptr);
}

} // namespace

void installFecesHooks() {
    if (!resolveField(classOf("Player__State__Std__Action__Hh__Nature"), "m", managerField)) {
        LOGE("Feces fields not found; the throw stays stock");
        return;
    }
    LOGI("Feces fields OK");

    installHook(target(T_FECES_THROW), reinterpret_cast<void*>(throwActionHook),
                "Player__State__Std__Action__Hh__Nature.Feces__Throw__Action");
    installHook(target(T_FECES_UPDATE), reinterpret_cast<void*>(throwUpdateHook),
                "Player__State__Std__Action__Hh__Nature.Feces__Throw__Action__Update");
}
