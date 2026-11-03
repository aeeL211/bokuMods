#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// The throw spawns its projectile and then asks for the empty hand id, which
// takes the piece out of the player's hands. Keeping that hand change back
// leaves the held piece in place, so the same piece can be thrown again.
void throwActionHook(void* self, int32_t throwState, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_FECES_THROW).orig);
    if (!orig)
        return;
    setKeepHand(true);
    orig(self, throwState, method);
    setKeepHand(false);
}

} // namespace

void installFecesHooks() {
    installHook(target(T_FECES_THROW), reinterpret_cast<void*>(throwActionHook),
                "Player__State__Std__Action__Hh__Nature.Feces__Throw__Action");
}
