#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "log.h"
#include "targets.h"

namespace {

// Hh__Change writes the new held id into the player model and rebuilds the hand
// sprite. A feature that keeps its item raises the flag around the stock call
// that would clear the hand, so that change becomes a no-op.
thread_local bool keepingHand = false;
// Last id the game put into the hand, kept so an eat end can put the same item
// back after its animation hid the sprite.
void* handOwner = nullptr;
int32_t handId = 0;
int refreshLogsLeft = 12;

void hhChangeHook(void* self, int32_t id, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!orig)
        return;
    if (keepingHand)
        return;
    orig(self, id, method);
    handOwner = self;
    handId = id;
}

} // namespace

void setKeepHand(bool keep) { keepingHand = keep; }

void refreshHand() {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!orig || !handOwner)
        return;
    orig(handOwner, handId, nullptr);
    if (refreshLogsLeft > 0) {
        refreshLogsLeft--;
        LOGI("Hand refreshed: id=%d", handId);
    }
}

void installHandHooks() {
    installHook(target(T_HH_CHANGE), reinterpret_cast<void*>(hhChangeHook),
                "Player__Manager.Hh__Change");
}
