#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field komponenUkuran = {};
Field tileManajer = {};
Field ukuranDuniaTile = {};
Field batasUkuranDunia = {};
Field multiUkuranDunia = {};
Field batasLebar = {};
Field batasTinggi = {};
Field presetTeratas = {};
Field lebarPreset = {};
Field tinggiPreset = {};
Field batasMulti = {};
bool batasSiap = false;

// Host memegang izin build dan destroy dan menolak apa pun yang dicoba guest di
// luar itu, jadi kedua flag sekarang diteruskan apa adanya. Hanya flag simpan
// dunia yang masih dipaksa, karena host mengirimnya sekali di awal dan guest
// tidak mendapat kesempatan lain untuk mengaktifkannya.
void hookIzinMulai(void* self, bool build, bool destroy, bool, void* method) {
    LOGI("Izin: mulai game build=%d destroy=%d", build, destroy);
    auto asli =
        reinterpret_cast<void (*)(void*, bool, bool, bool, void*)>(target(T_PERMISSION_START).orig);
    if (!asli)
        return;
    asli(self, build, destroy, true, method);
}

void hookIzinSimpan(void* self, bool, void* method) {
    LOGI("Izin: simpan dunia diterima");
    auto asli = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PERMISSION_SAVE).orig);
    if (!asli)
        return;
    asli(self, true, method);
}

// Daftar room membuang setiap room yang luas dunianya di atas batas perangkat
// ini, dan panel join serta info memakai tes yang sama. Menjawab true membuat
// room itu tetap terdaftar berapa pun ukurannya.
bool hookUkuranRoom(void*, int, int, void*) { return true; }

// Halaman ukuran membaca max.w dan max.h sebagai batas slider, tetapi manajer
// tile menurunkan batas itu ke tier perangkat ini saat boot, jauh sebelum hook
// ada. Mengembalikan preset teratas milik game setiap kali halaman dibuka
// melonggarkan batas itu tanpa angka buatan sendiri, dan luas room mengikuti
// batas yang baru.
void naikkanBatasDunia(void* self) {
    if (!batasSiap)
        return;
    void* manajer = bacaPtr(self, komponenUkuran);
    void* tile = bacaPtr(manajer, tileManajer);
    void* dunia = bacaPtr(tile, ukuranDuniaTile);
    void* batas = bacaPtr(dunia, batasUkuranDunia);
    void* preset = bacaPtr(batas, presetTeratas);
    int32_t lebar = bacaInt(preset, lebarPreset);
    int32_t tinggi = bacaInt(preset, tinggiPreset);
    if (lebar <= 0 || tinggi <= 0)
        return;
    int32_t lebarLama = bacaInt(batas, batasLebar);
    int32_t tinggiLama = bacaInt(batas, batasTinggi);
    if (lebarLama >= lebar && tinggiLama >= tinggi)
        return;
    tulisInt(batas, batasLebar, lebar);
    tulisInt(batas, batasTinggi, tinggi);
    tulisInt(bacaPtr(dunia, multiUkuranDunia), batasMulti, lebar * tinggi);
    LOGI("Batas ukuran dunia naik dari %dx%d ke %dx%d", lebarLama, tinggiLama, lebar, tinggi);
}

// Halaman ini menyusun slider dan ukuran saat ini di dalam panggilan ini, jadi
// batas dinaikkan sebelum fungsi asli berjalan dan bukaan pertama sudah
// melihatnya.
void hookBatas(void* self, void* method) {
    naikkanBatasDunia(self);
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_WORLD_CAP).orig);
    if (asli)
        asli(self, method);
}

bool cariFieldUkuranDunia() {
    if (!cariField(kelasDari("UI__Menu__World__New__Setting__Size"), "a", komponenUkuran))
        return false;
    void* kelasManajer = kelasField(komponenUkuran);
    if (!cariField(kelasManajer, "tile", tileManajer))
        return false;
    void* kelasTile = kelasField(tileManajer);
    if (!cariField(kelasTile, "_world_Size", ukuranDuniaTile))
        return false;
    void* kelasDunia = kelasField(ukuranDuniaTile);
    if (!cariField(kelasDunia, "max", batasUkuranDunia) ||
        !cariField(kelasDunia, "multi", multiUkuranDunia))
        return false;
    void* kelasBatas = kelasField(batasUkuranDunia);
    if (!cariField(kelasBatas, "w", batasLebar) || !cariField(kelasBatas, "h", batasTinggi) ||
        !cariField(kelasBatas, "_3", presetTeratas))
        return false;
    void* kelasPreset = kelasField(presetTeratas);
    return cariField(kelasPreset, "w", lebarPreset) &&
           cariField(kelasPreset, "h", tinggiPreset) &&
           cariField(kelasField(multiUkuranDunia), "max", batasMulti);
}

} // namespace

void pasangHookMultiplayer() {
    pasangHook(target(T_PERMISSION_START), reinterpret_cast<void*>(hookIzinMulai));
    pasangHook(target(T_PERMISSION_SAVE), reinterpret_cast<void*>(hookIzinSimpan));
    pasangHook(target(T_ROOM_SIZE), reinterpret_cast<void*>(hookUkuranRoom));
    batasSiap = cariFieldUkuranDunia();
    LOGI("Field ukuran dunia %s", batasSiap ? "OK" : "tidak ada, batas tetap per perangkat");
    pasangHook(target(T_WORLD_CAP), reinterpret_cast<void*>(hookBatas));
}
