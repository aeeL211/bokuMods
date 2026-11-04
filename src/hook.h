#pragma once

#include "targets.h"

// Pembungkus tipis ShadowHook. Setiap hook lewat sini supaya kegagalan tercatat
// di satu tempat dan pemanggil bisa membiarkan fitur itu pada perilaku vanilla.
bool inisialisasiShadowHook();
bool pasangHook(Target& entri, void* pengganti);

// Memasang hook pada alamat mentah, bukan pada entri target. Probe crash memakainya
// untuk fungsi libc yang tidak dinamai target mana pun, dan tidak ikut dihitung
// sebagai hook.
bool pasangHookProbe(void* alamat, void* pengganti, void** asli, const char* nama);

// Jumlah hook yang saat ini terpasang.
int jumlahHookTerpasang();
