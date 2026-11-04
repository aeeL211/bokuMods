#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

using FnKunci = void* (*)(void*, void*);

// Paket khusus host lolos di penerima karena payload-nya membawa kunci pengirim,
// dan penerima hanya membandingkan string itu. Penggantian ini hidup di thread
// pengirim dan dibersihkan begitu paketnya pergi.
thread_local bool spoofAktif = false;
thread_local void* kunciSpoof = nullptr;

void* hookKunci(void* sendiri, void* metode) {
    if (spoofAktif && kunciSpoof)
        return kunciSpoof;
    auto asli = reinterpret_cast<FnKunci>(target(T_PLAYER_KEY).orig);
    return asli ? asli(sendiri, metode) : nullptr;
}

} // namespace

void* kunciHostDari(void* multi) {
    auto kunci = reinterpret_cast<FnKunci>(target(T_HOST_KEY).addr);
    if (!multi || !kunci)
        return nullptr;
    return kunci(multi, target(T_HOST_KEY).method);
}

// Hanya berlaku sampai hentiSpoof(), jadi pemanggil membungkus satu panggilan
// kirim saja dan sisa permainan tetap memakai kunci pemain sendiri.
void mulaiSpoof(void* multi) {
    kunciSpoof = kunciHostDari(multi);
    spoofAktif = kunciSpoof != nullptr;
}

void hentiSpoof() {
    spoofAktif = false;
    kunciSpoof = nullptr;
}

void pasangHookSpoof() {
    if (!target(T_PLAYER_KEY).addr || !target(T_HOST_KEY).addr) {
        LOGE("Target spoof pengirim tidak ditemukan");
        return;
    }
    pasangHook(target(T_PLAYER_KEY), reinterpret_cast<void*>(hookKunci));
}
