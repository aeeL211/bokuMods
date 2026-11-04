#include "hooks.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// String model berisi beberapa ratus field yang dipisah. Batas ini hanya
// menghentikan panjang yang rusak agar tidak berjalan melewati string.
constexpr int32_t kBatasKarakterModel = 16384;
constexpr size_t kBatasByteModel = static_cast<size_t>(kBatasKarakterModel) * 3 + 1;
// Satu entri per skin berbeda yang bisa tampil dalam satu sesi; pengulangan hanya
// memakan satu perbandingan.
constexpr int kBatasHash = 512;
constexpr int kBatasSlot = 1000;
constexpr size_t kBatasBytePath = 1024;
// Id pengguna itu pendek; yang lebih panjang tidak layak jadi entri map.
constexpr size_t kBatasBytePengguna = 64;

char teksModel[kBatasByteModel];
char teksPath[kBatasBytePath];
char teksBerkas[kBatasByteModel];

uint64_t hashModel[kBatasHash];
int jumlahHashModel = 0;
int slotBerikut = 1;
bool slotSiap = false;

// Slot yang tersimpan dan pengguna asalnya, supaya nama tampilan yang datang
// belakangan bisa ditulis di samping model miliknya.
struct SlotPengguna {
    char pengguna[kBatasBytePengguna];
    int slot;
};

SlotPengguna daftarSlotPengguna[kBatasHash];
int jumlahSlotPengguna = 0;

Field fieldTamu = {};
Field fieldDataManajer = {};
Field fieldModel = {};
Field fieldNamaBerkas = {};
bool fieldSiap = false;
bool sudahCatatTidakSiap = false;

uint64_t hashTeks(const char* teks, size_t panjang) {
    uint64_t hasil = 1469598103934665603ull;
    for (size_t i = 0; i < panjang; i++) {
        hasil ^= static_cast<uint8_t>(teks[i]);
        hasil *= 1099511628211ull;
    }
    return hasil;
}

bool hashTercatat(uint64_t nilai) {
    for (int i = 0; i < jumlahHashModel; i++) {
        if (hashModel[i] == nilai)
            return true;
    }
    return false;
}

void catatHash(uint64_t nilai) {
    if (jumlahHashModel < kBatasHash)
        hashModel[jumlahHashModel++] = nilai;
}

// Folder model dimiliki Manager yang sama dengan pemegang daftar tamu, jadi
// penulis milik game sendiri menaruh berkasnya di Save/Model tanpa menebak path.
// Nama berkasnya ada di pemegang yang belum dimiliki penyimpanan setengah jadi,
// dan pembangun path melakukan dereference ke sana, jadi penyimpanan seperti itu
// dilaporkan tidak ada.
void* modelData(void* manajerTamu) {
    if (!fieldSiap)
        return nullptr;
    void* manajer = bacaPtr(manajerTamu, fieldTamu);
    void* data = bacaPtr(manajer, fieldDataManajer);
    void* model = bacaPtr(data, fieldModel);
    return bacaPtr(model, fieldNamaBerkas) ? model : nullptr;
}

// Path milik game sendiri untuk sebuah slot model, jadi tata letaknya tidak
// pernah ditebak di sini.
bool pathSlot(void* model, int slot) {
    auto panggil =
        reinterpret_cast<void* (*)(void*, int32_t, void*)>(target(T_DATA_MODEL_PATH).addr);
    return panggil && keUtf8(panggil(model, slot, target(T_DATA_MODEL_PATH).method), teksPath,
                             sizeof(teksPath));
}

bool slotAda(void* model, int slot) {
    return pathSlot(model, slot) && access(teksPath, F_OK) == 0;
}

void tulisSlot(void* model, int slot, void* text) {
    auto panggil =
        reinterpret_cast<void (*)(void*, int32_t, void*, void*)>(target(T_DATA_MODEL_WRITE).addr);
    if (panggil)
        panggil(model, slot, text, target(T_DATA_MODEL_WRITE).method);
}

// Game menulis nama tampilan modelnya sendiri dengan cara yang sama, jadi slot
// mendapat Name.txt biasa di samping Data.txt.
void tulisNamaSlot(void* model, int slot, void* text) {
    auto panggil =
        reinterpret_cast<void (*)(void*, int32_t, void*, void*)>(target(T_DATA_MODEL_NAME).addr);
    if (panggil)
        panggil(model, slot, text, target(T_DATA_MODEL_NAME).method);
}

// Skin dari sesi sebelumnya sudah memiliki slotnya, jadi hash setiap Data.txt di
// disk dimuat sekali dan penulisan berikutnya melanjutkan setelahnya.
bool bacaSlot(void* model, int slot) {
    if (!pathSlot(model, slot))
        return false;
    int fd = open(teksPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, teksBerkas, sizeof(teksBerkas) - 1);
    close(fd);
    if (n > 0)
        catatHash(hashTeks(teksBerkas, static_cast<size_t>(n)));
    return true;
}

void isiSlotAwal(void* model) {
    int slot = 1;
    while (slot <= kBatasSlot && bacaSlot(model, slot))
        slot++;
    slotBerikut = slot;
    LOGI("Slot skin: %d tersimpan, berikutnya %d", jumlahHashModel, slotBerikut);
}

// Setiap tamu datang dengan string yang disimpan game sendiri di Data.txt, jadi
// satu salinan per skin berbeda masuk ke slot model kosong pertama. Slot itu
// dikembalikan supaya namanya bisa ditempelkan begitu profil tiba.
int simpanModel(void* manajerTamu, void* modelStr) {
    if (!fieldSiap || !modelStr)
        return 0;
    void* model = modelData(manajerTamu);
    if (!model) {
        if (fieldSiap && !sudahCatatTidakSiap) {
            sudahCatatTidakSiap = true;
            LOGE("Model skin belum siap, skin setelah ini dilewati");
        }
        return 0;
    }
    if (!keUtf8(modelStr, teksModel, sizeof(teksModel)))
        return 0;

    if (!slotSiap) {
        slotSiap = true;
        isiSlotAwal(model);
    }
    uint64_t nilai = hashTeks(teksModel, strlen(teksModel));
    if (hashTercatat(nilai))
        return 0;
    if (slotBerikut > kBatasSlot) {
        LOGE("Slot skin penuh, %d skin tersimpan", jumlahHashModel);
        return 0;
    }

    tulisSlot(model, slotBerikut, modelStr);
    if (!slotAda(model, slotBerikut)) {
        LOGE("Penulisan skin gagal: slot %d", slotBerikut);
        return 0;
    }
    catatHash(nilai);
    LOGI("Skin tersimpan: slot %d (%zu byte)", slotBerikut, strlen(teksModel));
    return slotBerikut++;
}

void catatPengguna(void* user, int slot) {
    char teks[kBatasBytePengguna];
    if (jumlahSlotPengguna >= kBatasHash || !keUtf8(user, teks, sizeof(teks)))
        return;
    memcpy(daftarSlotPengguna[jumlahSlotPengguna].pengguna, teks, strlen(teks) + 1);
    daftarSlotPengguna[jumlahSlotPengguna].slot = slot;
    jumlahSlotPengguna++;
}

// Tamu membawa nama profil hanya setelah handshake join, jadi nama ditulis di
// sini, ke setiap slot tempat model pengguna itu disimpan.
void tulisNamaPengguna(void* manajerTamu, void* user, void* name) {
    char teks[kBatasBytePengguna];
    if (!fieldSiap || !name || panjangString(name) == 0 || !keUtf8(user, teks, sizeof(teks)))
        return;
    void* model = modelData(manajerTamu);
    if (!model)
        return;

    for (int i = 0; i < jumlahSlotPengguna; i++) {
        if (strcmp(daftarSlotPengguna[i].pengguna, teks))
            continue;
        tulisNamaSlot(model, daftarSlotPengguna[i].slot, name);
        LOGI("Nama skin: slot %d", daftarSlotPengguna[i].slot);
    }
}

using FnTambah = void (*)(void*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void*,
                          void*, void*, void*, void*, bool, bool, bool, bool, bool, bool, void*);

// Daftar tamu dibangun dari satu panggilan ini di host maupun client, dan
// argumen Model-nya adalah string skin penuh, jadi tidak ada yang perlu diurai
// di sini.
void hookTambah(void* self, void* user, int32_t z, int32_t x, int32_t y, int32_t dir, int32_t s,
                int32_t obj, void* key, void* dollName, void* parent, void* model, void* ping,
                bool build, bool detonate, bool saveWorld, bool dollInteract, bool dollMove,
                bool dollSetting, void* method) {
    int slot = simpanModel(self, model);
    if (slot)
        catatPengguna(user, slot);

    auto asli = reinterpret_cast<FnTambah>(target(T_GUEST_ADD).orig);
    if (asli)
        asli(self, user, z, x, y, dir, s, obj, key, dollName, parent, model, ping, build, detonate,
             saveWorld, dollInteract, dollMove, dollSetting, method);
}

using FnNama = void (*)(void*, void*, void*, void*);

// Profil datang setelah join, jadi nama tampilan ditulis ke slot tempat model
// tamu itu disimpan.
void hookNama(void* self, void* user, void* name, void* method) {
    auto asli = reinterpret_cast<FnNama>(target(T_GUEST_NAME).orig);
    if (asli)
        asli(self, user, name, method);
    tulisNamaPengguna(self, user, name);
}

} // namespace

void pasangHookSkin() {
    void* kelasTamu = kelasDari("Guest__Manager");
    fieldSiap = cariField(kelasTamu, "a", fieldTamu);
    void* kelasManajer = fieldSiap ? kelasField(fieldTamu) : nullptr;
    fieldSiap = fieldSiap && cariField(kelasManajer, "data", fieldDataManajer);
    void* kelasData = fieldSiap ? kelasField(fieldDataManajer) : nullptr;
    fieldSiap = fieldSiap && cariField(kelasData, "model", fieldModel);
    void* kelasModel = fieldSiap ? kelasField(fieldModel) : nullptr;
    fieldSiap = fieldSiap && cariField(kelasModel, "_filename", fieldNamaBerkas);
    LOGI("Field model skin %s", fieldSiap ? "OK" : "tidak ada, skin tidak disentuh");

    pasangHook(target(T_GUEST_ADD), reinterpret_cast<void*>(hookTambah));
    pasangHook(target(T_GUEST_NAME), reinterpret_cast<void*>(hookNama));
}
