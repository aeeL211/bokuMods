#include "hooks.h"

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
}
