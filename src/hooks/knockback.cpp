#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Explosions call this to tumble the local player away from the blast. Dropping
// the call leaves the player standing while the tiles and effects still break.
void tumbleHook(void*, int32_t, float, float, int32_t, void*) {}

} // namespace

void installKnockbackHooks() {
    installHook(target(T_TUMBLE_EXPLODE), reinterpret_cast<void*>(tumbleHook),
                "Player__State__Std__Air.Tumble__Explode");
}
