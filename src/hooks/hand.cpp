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
// An eat end asks for the hand back, but a rebuild while the eat state is still
// current builds the hidden eating pose. The rebuild waits for the end of that
// player's update, when the state machine has moved past the eat animation.
bool refreshPending = false;
int refreshLogsLeft = 12;

void rebuildHand(void* self) {
    auto hh = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!hh)
        return;
    hh(self, handId, target(T_HH_CHANGE).method);
    if (refreshLogsLeft > 0) {
        refreshLogsLeft--;
        LOGI("Hand refreshed: id=%d", handId);
    }
}

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

void updateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PLAYER_UPDATE).orig);
    if (!orig)
        return;
    orig(self, method);
    if (!refreshPending || self != handOwner)
        return;
    refreshPending = false;
    rebuildHand(self);
}

} // namespace

void setKeepHand(bool keep) { keepingHand = keep; }

void refreshHand() {
    if (!handOwner)
        return;
    // Without the update hook there is no safe place to rebuild later, so fall
    // back to the moment the eat end returns.
    if (!target(T_PLAYER_UPDATE).orig) {
        rebuildHand(handOwner);
        return;
    }
    refreshPending = true;
}

void installHandHooks() {
    installHook(target(T_HH_CHANGE), reinterpret_cast<void*>(hhChangeHook));
    installHook(target(T_PLAYER_UPDATE), reinterpret_cast<void*>(updateHook));
}
