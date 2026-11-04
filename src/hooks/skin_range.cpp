#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field fieldMPos = {};
Field fieldPosUtama = {};
Field fieldLangkahMaks = {};
Field fieldLangkahSetengah = {};
Field fieldLebarLangkah = {};
Field fieldLebarPos = {};
Field fieldNilaiPos = {};
Field fieldNilaiMin = {};
Field fieldNilaiMaks = {};
Field fieldNilaiPerLangkah = {};
Field fieldMPotret = {};
Field fieldPosPotret = {};

constexpr int kMaksSumbu = 8;
constexpr int kSlotRentang = 2;
int32_t skinDasar[kSlotRentang][kMaksSumbu] = {};
int32_t skinDiperlebar[kSlotRentang][kMaksSumbu] = {};

// Refresh__Step mengatur ulang slider X/Y ke -5..+5 lalu menurunkan steps_Half,
// steps_W, dan per_Step dari steps_Max. Hanya sumbu yang masih -5..+5 yang
// dilebarkan (sumbu rotasi dibiarkan) dan nilai turunannya disusun ulang supaya
// Value__To__Step melihat rentang -25..+25 yang baru pada lintasan Refresh() yang
// sama.
void perlebarRentang(void* self, const Field& fieldPemilik, const Field& fieldPos, int slot) {
    if (!fieldPemilik.field || !fieldPos.field || !fieldLangkahMaks.field ||
        !fieldLangkahSetengah.field || !fieldLebarLangkah.field || !fieldLebarPos.field ||
        !fieldNilaiPos.field || !fieldNilaiMin.field || !fieldNilaiMaks.field ||
        !fieldNilaiPerLangkah.field) {
        return;
    }

    void* pemilik = bacaPtr(self, fieldPemilik);
    void* pos = bacaPtr(pemilik, fieldPos);
    if (!pos)
        return;

    void* nilaiArr = bacaPtr(pos, fieldNilaiPos);
    void* langkahMaks = bacaPtr(pos, fieldLangkahMaks);
    void* langkahSetengah = bacaPtr(pos, fieldLangkahSetengah);
    void* lebarLangkah = bacaPtr(pos, fieldLebarLangkah);
    if (!nilaiArr || !langkahMaks || !langkahSetengah || !lebarLangkah)
        return;

    int32_t jumlahNilai = panjangArray(nilaiArr);
    int32_t jumlahMaks = panjangArray(langkahMaks);
    int32_t jumlahSetengah = panjangArray(langkahSetengah);
    int32_t jumlahLebar = panjangArray(lebarLangkah);
    int32_t jumlah = jumlahNilai < jumlahMaks ? jumlahNilai : jumlahMaks;
    if (jumlah > kMaksSumbu)
        jumlah = kMaksSumbu;
    if (jumlah <= 0)
        return;

    float lebarPos = bacaFloat(pos, fieldLebarPos);

    for (int32_t i = 0; i < jumlah; i++) {
        void* nilai = *reinterpret_cast<void**>(dataArray(nilaiArr) + i * sizeof(void*));
        if (!nilai)
            continue;

        float batasMin = bacaFloat(nilai, fieldNilaiMin);
        float batasMaks = bacaFloat(nilai, fieldNilaiMaks);
        if (!dekatNilai(batasMin, -5.0f) || !dekatNilai(batasMaks, 5.0f))
            continue;

        char* alamatMaks = dataArray(langkahMaks) + i * sizeof(int32_t);
        int32_t* ptrMaks = reinterpret_cast<int32_t*>(alamatMaks);
        int32_t sekarang = *ptrMaks;
        if (sekarang <= 0) {
            LOGE("Rentang skin: sumbu %d punya steps_Max=%d, dilewati", i, sekarang);
            continue;
        }
        if (sekarang > INT32_MAX / 5) {
            LOGE("Rentang skin: sumbu %d punya steps_Max=%d, dilewati", i, sekarang);
            continue;
        }

        int32_t dasar;
        int32_t diperlebar;
        if (skinDasar[slot][i] != 0 && sekarang == skinDasar[slot][i]) {
            dasar = skinDasar[slot][i];
            diperlebar = skinDiperlebar[slot][i];
        } else if (skinDiperlebar[slot][i] != 0 && sekarang == skinDiperlebar[slot][i]) {
            dasar = skinDasar[slot][i];
            diperlebar = skinDiperlebar[slot][i];
        } else {
            dasar = sekarang;
            diperlebar = sekarang * 5;
            skinDasar[slot][i] = dasar;
            skinDiperlebar[slot][i] = diperlebar;
            LOGI("Rentang skin: sumbu %d dilebarkan %d -> %d langkah", i, dasar, diperlebar);
        }

        *ptrMaks = diperlebar;
        if (i < jumlahSetengah) {
            char* alamatSetengah = dataArray(langkahSetengah) + i * sizeof(int32_t);
            *reinterpret_cast<int32_t*>(alamatSetengah) = diperlebar / 2;
        }
        if (i < jumlahLebar && lebarPos > 0.0f) {
            char* alamatLebar = dataArray(lebarLangkah) + i * sizeof(float);
            *reinterpret_cast<float*>(alamatLebar) = lebarPos / static_cast<float>(diperlebar);
        }

        tulisFloat(nilai, fieldNilaiMin, -25.0f);
        tulisFloat(nilai, fieldNilaiMaks, 25.0f);
        tulisFloat(nilai, fieldNilaiPerLangkah, 10.0f / static_cast<float>(dasar));
    }
}

void hookLangkahPos(void* self, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_POS_REFRESH_STEP).orig);
    if (!asli)
        return;
    asli(self, method);
    perlebarRentang(self, fieldMPos, fieldPosUtama, 0);
}

// Editor potret punya instance _Pos dan array langkah sendiri, jadi ia memakai
// slot memo kedua alih-alih menimpa milik editor model.
void hookLangkahPotret(void* self, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_PORTRAIT_REFRESH_STEP).orig);
    if (!asli)
        return;
    asli(self, method);
    perlebarRentang(self, fieldMPotret, fieldPosPotret, 1);
}

bool cariFieldPos(void* kelasPos) {
    return cariField(kelasPos, "steps_Max", fieldLangkahMaks) &&
           cariField(kelasPos, "steps_Half", fieldLangkahSetengah) &&
           cariField(kelasPos, "steps_W", fieldLebarLangkah) &&
           cariField(kelasPos, "w", fieldLebarPos) &&
           cariField(kelasPos, "values", fieldNilaiPos) &&
           cariField(kelasElemenField(fieldNilaiPos), "min", fieldNilaiMin) &&
           cariField(kelasElemenField(fieldNilaiPos), "max", fieldNilaiMaks) &&
           cariField(kelasElemenField(fieldNilaiPos), "per_Step", fieldNilaiPerLangkah);
}

} // namespace

void pasangHookRentangSkin() {
    void* komponenPos = kelasDari("UI__Menu__Model__Main__Pos");
    bool rentangModel = cariField(komponenPos, "m", fieldMPos) &&
                        cariField(kelasField(fieldMPos), "_pos", fieldPosUtama);

    void* komponenPotret = kelasDari("Net__Account__Portrait__Main__Pos");
    bool rentangPotret = cariField(komponenPotret, "m", fieldMPotret) &&
                         cariField(kelasField(fieldMPotret), "_pos", fieldPosPotret);

    bool fieldPosAda =
        cariFieldPos(rentangModel ? kelasField(fieldPosUtama) : kelasField(fieldPosPotret));
    if (rentangModel && fieldPosAda) {
        LOGI("Field rentang skin OK");
    } else {
        LOGE("Field rentang skin tidak ditemukan; X/Y tetap -5..5");
    }
    if (!rentangPotret || !fieldPosAda) {
        LOGE("Field rentang potret tidak ditemukan; X/Y potret tetap -5..5");
    }

    pasangHook(target(T_POS_REFRESH_STEP), reinterpret_cast<void*>(hookLangkahPos));
    pasangHook(target(T_PORTRAIT_REFRESH_STEP), reinterpret_cast<void*>(hookLangkahPotret));
}
