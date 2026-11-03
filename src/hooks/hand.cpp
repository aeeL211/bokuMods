#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Hh__Change writes the new held id into the player model and rebuilds the hand
// sprite. A feature that keeps its item raises the flag around the stock call
// that would clear the hand, so that change becomes a no-op.
thread_local bool keepingHand = false;
// Last id the game put into the hand, and the player state that showed it.
void* handOwner = nullptr;
int32_t handId = 0;
int32_t shownState = 0;
// The eat end leaves the player in that food's finished state, and a state's
// own sprite build is what turns the hand layer off, so the kept item stays
// invisible until the next state change, which is why a move brings it back.
// Rebuilding the sprite for the state the item was shown in puts the hand back
// without touching the state machine.
Field sprField = {};
Field stateField = {};
bool refreshPending = false;
int refreshLogsLeft = 12;

void rebuildHand(void* self) {
    auto hh = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (hh)
        hh(self, handId, nullptr);
}

// The state build draws the hand layer from the unchanged model id, so the pose
// the item was picked up in shows it again while the player state stays as is.
void rebuildSprite(void* self) {
    auto set = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_SPR_SET).addr);
    void* spr = readPtr(self, sprField);
    if (set && spr && shownState != 0)
        set(spr, shownState, target(T_SPR_SET).method);
}

void restoreHand(void* self) {
    rebuildHand(self);
    rebuildSprite(self);
    if (refreshLogsLeft > 0) {
        refreshLogsLeft--;
        LOGI("Hand refreshed: id=%d, state=%d", handId, shownState);
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
    // A non-zero id means the item is in hand and its pose draws it, so this
    // state is the one to come back to after an eat end.
    if (id != 0)
        shownState = readInt(self, stateField);
}

void updateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PLAYER_UPDATE).orig);
    if (!orig)
        return;
    orig(self, method);
    if (!refreshPending || self != handOwner)
        return;
    refreshPending = false;
    restoreHand(self);
}

void resolveHandFields() {
    void* manager = classOf("Player__Manager");
    bool ok = resolveField(manager, "spr", sprField) && resolveField(manager, "state", stateField);
    if (ok)
        LOGI("Hand fields OK");
    else
        LOGE("Hand fields not found; a kept food waits for the next move");
}

} // namespace

void setKeepHand(bool keep) { keepingHand = keep; }

void refreshHand() {
    if (!handOwner)
        return;
    // Without the update hook there is no later tick, so the hand is restored
    // where the eat end returns.
    if (!target(T_PLAYER_UPDATE).orig) {
        restoreHand(handOwner);
        return;
    }
    refreshPending = true;
}

void installHandHooks() {
    resolveHandFields();
    installHook(target(T_HH_CHANGE), reinterpret_cast<void*>(hhChangeHook),
                "Player__Manager.Hh__Change");
    installHook(target(T_PLAYER_UPDATE), reinterpret_cast<void*>(updateHook),
                "Player__Manager._Update");
}
