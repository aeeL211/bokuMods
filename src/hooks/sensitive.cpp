#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Setiap pemeriksaan teks di game diputuskan di sini: daftar kata ditanyakan
// lebih dulu, dan bila pemanggil juga meminta catatan, catatan itu dimunculkan.
// Menjawab false melewati keduanya, jadi baris chat, nama, dan teks profil lolos.
bool hookSensitif(void*, void*, int32_t, bool, void*) { return false; }

} // namespace

void pasangHookSensitif() {
    pasangHook(target(T_SENSITIVE_WORD), reinterpret_cast<void*>(hookSensitif));
}
