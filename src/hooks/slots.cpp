#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Game membawa 9999 slot per jenis penyimpanan. INT32_MAX membuat setiap
// penyimpanan tanpa batas, yang ditawarkan mod ini sebagai ganti membeli slot
// satu per satu.
constexpr int32_t kBatasSlot = INT32_MAX;

// Setiap jenis penyimpanan adalah field Global, mungkin satu tingkat di bawahnya,
// yang kelasnya membawa penghitung data_Slot_Num yang diisi loader saat mulai.
struct Slot {
    const char* pertama;
    const char* kedua;
    Field fieldPertama;
    Field fieldKedua;
    Field fieldJumlah;
};

Slot slots[] = {
    {"model", nullptr, {}, {}, {}},       {"face", nullptr, {}, {}, {}},
    {"portrait", nullptr, {}, {}, {}},    {"photo", nullptr, {}, {}, {}},
    {"_favorite", nullptr, {}, {}, {}},   {"_melody", nullptr, {}, {}, {}},
    {"world", nullptr, {}, {}, {}},       {"doll", nullptr, {}, {}, {}},
    {"toolbar", "face", {}, {}, {}},      {"toolbar", "hh", {}, {}, {}},
    {"_paint", "painting", {}, {}, {}},   {"_paint", "palette", {}, {}, {}},
    {"_block", "pack", {}, {}, {}},       {"_block", "own", {}, {}, {}},
    {"_block", "list", {}, {}, {}},       {"_jukebox", "library", {}, {}, {}},
    {"_jukebox", "playlist", {}, {}, {}},
};

bool cariFieldSlot(Slot& slot) {
    void* kelasGlobal = kelasDari("Global");
    if (!cariField(kelasGlobal, slot.pertama, slot.fieldPertama))
        return false;

    void* kelasPemegang = kelasField(slot.fieldPertama);
    if (slot.kedua) {
        if (!cariField(kelasPemegang, slot.kedua, slot.fieldKedua))
            return false;
        kelasPemegang = kelasField(slot.fieldKedua);
    }
    return cariField(kelasPemegang, "data_Slot_Num", slot.fieldJumlah);
}

// Loader bisa saja sudah berjalan sebelum hook ini dipasang, jadi nilai yang
// ditinggalkannya dinaikkan sekali saat start juga.
void naikkanSlot(void* dasar, const Slot& slot) {
    if (!slot.fieldJumlah.field)
        return;

    void* pemegang = bacaPtr(dasar, slot.fieldPertama);
    if (slot.kedua)
        pemegang = bacaPtr(pemegang, slot.fieldKedua);
    if (pemegang && bacaInt(pemegang, slot.fieldJumlah) < kBatasSlot)
        tulisInt(pemegang, slot.fieldJumlah, kBatasSlot);
}

// Setiap jenis penyimpanan memuat jumlah slot miliknya lewat satu fungsi ini.
// Menaikkan jumlahnya ke batas membuka seluruh penyimpanan tanpa membeli slot
// satu per satu.
void hookMuat(int32_t* count, void* key, int32_t standalone, void* method) {
    auto asli =
        reinterpret_cast<void (*)(int32_t*, void*, int32_t, void*)>(target(T_SLOT_LOAD).orig);
    if (!asli)
        return;

    asli(count, key, standalone, method);
    if (count && *count < kBatasSlot)
        *count = kBatasSlot;
}

} // namespace

void pasangHookSlot() {
    void* dasar = dataFieldStatis(kelasDari("Global"));

    int siap = 0;
    for (Slot& slot : slots) {
        if (!cariFieldSlot(slot)) {
            if (slot.kedua)
                LOGE("Field slot tidak ditemukan: %s.%s", slot.pertama, slot.kedua);
            else
                LOGE("Field slot tidak ditemukan: %s", slot.pertama);
            continue;
        }
        naikkanSlot(dasar, slot);
        siap++;
    }

    int jumlah = static_cast<int>(sizeof(slots) / sizeof(slots[0]));
    if (siap == jumlah)
        LOGI("Field slot OK");
    else
        LOGE("Field slot: %d dari %d ditemukan", siap, jumlah);
    LOGI("Batas slot %d", kBatasSlot);

    pasangHook(target(T_SLOT_LOAD), reinterpret_cast<void*>(hookMuat));
}
