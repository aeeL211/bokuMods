#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field sprField = {};
Field spinField = {};

// A landed feces starts its removal countdown, and one that sits too long
// starts a fade. Both paths are dropped so a thrown piece stays on the ground.
// The stock state keeps spinning the sprite, so the spin is stopped at the same
// time and the piece rests at its impact angle.
void stopSpin(void* self) {
    void* spr = readPtr(self, sprField);
    if (spr)
        writeFloat(spr, spinField, 0.0f);
}

void disappearHook(void* self, void*) { stopSpin(self); }
void removeHook(void* self, void*) { stopSpin(self); }

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
    void* fecesClass = classOf("Obj__Feces__Obj");
    if (resolveField(fecesClass, "_spr", sprField) &&
        resolveField(fieldClass(sprField), "rotate__Speed", spinField)) {
        LOGI("Feces fields OK");
    } else {
        LOGE("Feces field not found; a thrown piece keeps spinning");
    }

    installHook(target(T_FECES_DISAPPEAR), reinterpret_cast<void*>(disappearHook),
                "Obj__Feces__Obj.State__Disappear");
    installHook(target(T_FECES_REMOVE), reinterpret_cast<void*>(removeHook),
                "Obj__Feces__Obj.State__Remove__Countdown");
    installHook(target(T_FECES_THROW), reinterpret_cast<void*>(throwActionHook),
                "Player__State__Std__Action__Hh__Nature.Feces__Throw__Action");
}
