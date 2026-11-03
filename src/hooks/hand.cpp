#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Hh__Change writes the new held id into the player model and rebuilds the hand
// sprite. A feature that keeps its item raises the flag around the stock call
// that would clear the hand, so that change becomes a no-op.
thread_local bool keepingHand = false;

void hhChangeHook(void* self, int32_t id, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!orig)
        return;
    if (keepingHand)
        return;
    orig(self, id, method);
}

} // namespace

void setKeepHand(bool keep) { keepingHand = keep; }

void installHandHooks() {
    installHook(target(T_HH_CHANGE), reinterpret_cast<void*>(hhChangeHook),
                "Player__Manager.Hh__Change");
}
