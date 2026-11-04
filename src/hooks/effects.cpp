#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Getar kamera murni visual, jadi kedua titik masuknya dibuang.
void hookGetar(void*, void*) {}
void hookGetarAngka(void*, int32_t, float, void*) {}

// Puing tile dimunculkan saat tile pecah. Membuang pemunculannya membiarkan tile
// dan rekamannya utuh sementara serpihannya tidak ada.
void hookPuing(void*, int32_t, void*) {}
void hookPuingTile(void*, float, float, int32_t, void*, void*) {}

} // namespace

void pasangHookEfek() {
    pasangHook(target(T_SHAKE), reinterpret_cast<void*>(hookGetar));
    pasangHook(target(T_SHAKE_NUM), reinterpret_cast<void*>(hookGetarAngka));
    pasangHook(target(T_DEBRIS), reinterpret_cast<void*>(hookPuing));
    pasangHook(target(T_DEBRIS_TILE), reinterpret_cast<void*>(hookPuingTile));
}
