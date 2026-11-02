#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Camera shake is purely visual, so both entry points are dropped.
void shakeHook(void*, void*) {}
void shakeNumHook(void*, int32_t, float, void*) {}

// Tile debris is spawned on break. Dropping the spawn leaves the tile and the
// record untouched while the fragments stay gone.
void debrisHook(void*, int32_t, void*) {}
void debrisTileHook(void*, float, float, int32_t, void*, void*) {}

} // namespace

void installEffectHooks() {
    installHook(target(T_SHAKE), reinterpret_cast<void*>(shakeHook), "Camera__Manager.Shake");
    installHook(target(T_SHAKE_NUM), reinterpret_cast<void*>(shakeNumHook),
                "Camera__Manager.Shake(int, float)");
    installHook(target(T_DEBRIS), reinterpret_cast<void*>(debrisHook), "Eff__Type__Tile.Debris");
    installHook(target(T_DEBRIS_TILE), reinterpret_cast<void*>(debrisTileHook),
                "Eff__Type__Tile.Debris__Tile");
}
