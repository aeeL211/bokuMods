#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "log.h"
#include "targets.h"

namespace {

// Hh__Change menulis id pegang yang baru ke model pemain dan membangun ulang
// sprite tangan. Fitur yang menahan itemnya menaikkan flag ini di sekitar
// panggilan bawaan yang akan mengosongkan tangan, jadi perubahan itu menjadi
// tanpa efek.
thread_local bool menahanTangan = false;
// Id terakhir yang dimasukkan game ke tangan, disimpan supaya akhir makan bisa
// mengembalikan item yang sama setelah animasinya menyembunyikan sprite.
void* pemilikTangan = nullptr;
int32_t idTangan = 0;
// Akhir makan meminta tangan kembali, tapi pembangunan ulang saat state makan
// masih aktif membangun pose makan yang tersembunyi. Pembangunan ulang menunggu
// akhir update pemain itu, saat state machine sudah melewati animasi makan.
bool segarkanTertunda = false;
int sisaLogSegarkan = 12;

void bangunUlangTangan(void* sendiri) {
    auto hh = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!hh)
        return;
    hh(sendiri, idTangan, target(T_HH_CHANGE).method);
    if (sisaLogSegarkan > 0) {
        sisaLogSegarkan--;
        LOGI("Tangan disegarkan: id=%d", idTangan);
    }
}

void hookGantiHh(void* sendiri, int32_t id, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_HH_CHANGE).orig);
    if (!asli)
        return;
    if (menahanTangan)
        return;
    asli(sendiri, id, metode);
    pemilikTangan = sendiri;
    idTangan = id;
}

void hookUpdate(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_PLAYER_UPDATE).orig);
    if (!asli)
        return;
    asli(sendiri, metode);
    if (!segarkanTertunda || sendiri != pemilikTangan)
        return;
    segarkanTertunda = false;
    bangunUlangTangan(sendiri);
}

} // namespace

void setelTahanTangan(bool tahan) { menahanTangan = tahan; }

void segarkanTangan() {
    if (!pemilikTangan)
        return;
    // Tanpa hook update tidak ada tempat aman untuk membangun ulang nanti, jadi
    // kembali ke saat akhir makan selesai.
    if (!target(T_PLAYER_UPDATE).orig) {
        bangunUlangTangan(pemilikTangan);
        return;
    }
    segarkanTertunda = true;
}

void pasangHookTangan() {
    pasangHook(target(T_HH_CHANGE), reinterpret_cast<void*>(hookGantiHh));
    pasangHook(target(T_PLAYER_UPDATE), reinterpret_cast<void*>(hookUpdate));
}
