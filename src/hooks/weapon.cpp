#include "hooks.h"

#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field fieldManajerSenjata = {};
Field fieldSenjataCmn = {};
Field fieldManajerState = {};
Field fieldManajerCmn = {};
Field fieldCmnHh = {};
Field fieldState = {};
Field fieldCombo = {};
Field fieldRock = {};
Field fieldCharge = {};
// Penghitung charge runtime adalah jumlah step 32-bit yang dibandingkan
// pencarian id buster dengan step penuh dari konfigurasi.
Field fieldStep = {};
Field fieldHhRock = {};
Field fieldConfigCharge = {};
Field fieldStepPenuh = {};

// Tombol yang ditahan menembak sekali per jendela. Pistol dan buster bawaan
// sama-sama menunggu timer aksi yang panjang, dan buster bisa menghabiskan
// interval tembaknya sebelum state akhir mulai, setelah itu tombol yang ditahan
// berhenti menembak.
constexpr long long kIramaPistolMs = 80;
constexpr long long kAksiBusterMs = 60;
constexpr long long kIramaBusterMs = 100;

// Setiap pemain menjalankan state machine yang sama, jadi iramanya mengikuti
// manajer pemain. Satu jam bersama akan membuat tamu mereset tepi tombol lokal.
struct Irama {
    void* manajer;
    long long tembakPistolMs;
    long long tembakBusterMs;
    bool ditahan;
};
constexpr int kJumlahIrama = 32;
Irama daftarIrama[kJumlahIrama] = {};
int iramaBerikut = 0;

long long sekarangMs() {
    timespec sekarang = {};
    clock_gettime(CLOCK_MONOTONIC, &sekarang);
    return static_cast<long long>(sekarang.tv_sec) * 1000 + sekarang.tv_nsec / 1000000;
}

Irama& iramaUntuk(void* manajer) {
    Irama* slotKosong = nullptr;
    for (Irama& irama : daftarIrama) {
        if (irama.manajer == manajer)
            return irama;
        if (!irama.manajer && !slotKosong)
            slotKosong = &irama;
    }

    Irama& slot = slotKosong ? *slotKosong : daftarIrama[iramaBerikut];
    iramaBerikut = (iramaBerikut + 1) % kJumlahIrama;
    slot = {};
    slot.manajer = manajer;
    return slot;
}

// Bernilai benar paling sering sekali per jendela, jadi tombol yang ditahan
// menembak dengan laju tetap.
bool iramaSiap(long long& terakhirMs, long long iramaMs) {
    long long sekarang = sekarangMs();
    if (sekarang - terakhirMs < iramaMs)
        return false;
    terakhirMs = sekarang;
    return true;
}

void* statePemain(void* sendiri, const Field& bidangManajer) {
    void* manajer = bacaPtr(sendiri, bidangManajer);
    return bacaPtr(manajer, fieldState);
}

// Update state mengulang tembakan hanya selama flag combo-nya menyala, dan flag
// itu mengikuti tombol yang ditahan. Menulisnya sebelum update bawaan membuat
// semua gaya berdiri maupun bergerak ikut mengulang.
void selaraskanCombo(void* manajer) {
    void* state = bacaPtr(manajer, fieldState);
    if (state)
        tulisBool(state, fieldCombo, tombolAksiDitekan(manajer));
}

// Tekanan tombol memulai tembakan bawaan, jadi irama dihitung dari sini dan
// pengulangan paksa pertama jatuh satu jendela kemudian, bukan seketika.
void hookUpdateState(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_STATE_UPDATE).orig);
    if (!asli)
        return;

    void* manajer = bacaPtr(sendiri, fieldManajerState);
    Irama& irama = iramaUntuk(manajer);
    bool ditahan = tombolAksiDitekan(manajer);
    if (ditahan && !irama.ditahan) {
        irama.tembakPistolMs = sekarangMs();
        irama.tembakBusterMs = irama.tembakPistolMs;
    }
    irama.ditahan = ditahan;
    asli(sendiri, metode);
}

// State akhir pistol adalah timer tetap. Mengakhirinya pada update pertama
// menghilangkan jeda antar tembakan; fase aksi tetap memutar muzzle dan peluru.
bool hookAkhirPistol(void*, void*) { return true; }

// Aksi pistol bawaan hanya berakhir setelah frame kedua plus timer aksi.
// Menjawab ya pada irama pendek selama tombol ditahan menembak lagi sebelum
// timer itu membiarkan jalur bawaan lewat.
bool hookAkhirAksiPistol(void* sendiri, int gambar, void* metode) {
    auto asli = reinterpret_cast<bool (*)(void*, int, void*)>(target(T_GUN_ACTION_END).orig);
    if (!asli)
        return false;

    void* manajer = bacaPtr(sendiri, fieldManajerCmn);
    Irama& irama = iramaUntuk(manajer);
    if (asli(sendiri, gambar, metode)) {
        if (tombolAksiDitekan(manajer))
            irama.tembakPistolMs = sekarangMs();
        return true;
    }
    if (!tombolAksiDitekan(manajer))
        return false;
    return iramaSiap(irama.tembakPistolMs, kIramaPistolMs);
}

// Jaga flag combo tetap mutakhir selama aksi pistol, lalu jalankan update bawaan.
void hookUpdatePistol(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_GUN_UPDATE).orig);
    if (!asli)
        return;

    selaraskanCombo(bacaPtr(sendiri, fieldManajerSenjata));
    asli(sendiri, metode);
}

// Penyelerasan yang sama selama aksi buster.
void hookUpdateBuster(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_UPDATE).orig);
    if (!asli)
        return;

    selaraskanCombo(bacaPtr(sendiri, fieldManajerSenjata));
    asli(sendiri, metode);
}

// Setiap gaya mengakhiri aksi buster lewat pengecekan ini, yang menunggu timer
// aksi panjang. Mengakhirinya satu jendela aksi setelah tembakan mulai
// mempertahankan frame muzzle dan efek sambil membuang sisa penantiannya.
bool hookAkhirAksiBuster(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_ACTION_END).orig);
    if (!asli)
        return false;

    void* manajer = bacaPtr(sendiri, fieldManajerCmn);
    Irama& irama = iramaUntuk(manajer);
    if (asli(sendiri, metode))
        return true;
    if (!tombolAksiDitekan(manajer))
        return false;
    return sekarangMs() - irama.tembakBusterMs >= kAksiBusterMs;
}

// Gaya bergerak meninggalkan state akhir buster lewat pengecekan ini, yang
// menyerah begitu interval tembakan lewat. Menjawab ya pada irama membuat
// tembakan terus berjalan selama tombol ditahan.
bool hookAkhirKeAksiBuster(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<bool (*)(void*, void*)>(target(T_ROCK_END_TO_ACTION).orig);
    if (!asli)
        return false;

    void* manajer = bacaPtr(sendiri, fieldManajerCmn);
    Irama& irama = iramaUntuk(manajer);
    if (asli(sendiri, metode)) {
        if (tombolAksiDitekan(manajer))
            irama.tembakBusterMs = sekarangMs();
        return true;
    }
    if (!tombolAksiDitekan(manajer))
        return false;
    return iramaSiap(irama.tembakBusterMs, kIramaBusterMs);
}

// Buster berdiri menembak ulang di dalam update akhirnya sendiri. Menahan update
// itu sampai iramanya jatuh tempo lalu memulai aksinya menjaga laju yang sama,
// dan jalur bawaan tetap berjalan setiap kali tombol dilepas.
void hookUpdateAkhirBuster(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_ROCK_END_UPDATE).orig);
    if (!asli)
        return;

    auto arahAkhir = reinterpret_cast<int32_t (*)(void*, void*)>(target(T_ROCK_END_DIR).addr);
    auto aksi = reinterpret_cast<void (*)(void*, int32_t, void*)>(target(T_ROCK_ACTION).addr);
    void* manajer = bacaPtr(sendiri, fieldManajerSenjata);
    void* cmn = bacaPtr(sendiri, fieldSenjataCmn);
    if (!arahAkhir || !aksi || !cmn || !tombolAksiDitekan(manajer)) {
        asli(sendiri, metode);
        return;
    }

    Irama& irama = iramaUntuk(manajer);
    if (!iramaSiap(irama.tembakBusterMs, kIramaBusterMs))
        return;

    aksi(sendiri, arahAkhir(cmn, target(T_ROCK_END_DIR).method), target(T_ROCK_ACTION).method);
}

// Pencarian id hanya mengembalikan proyektil terisi bila step charge runtime
// sama dengan step penuh dari konfigurasi. Menulis step itu sebelum pencarian
// membuat setiap tembakan memakai amunisi terkuat.
int32_t hookIdBuster(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<int32_t (*)(void*, void*)>(target(T_ROCK_ID).orig);
    if (!asli)
        return 0;

    void* state = statePemain(sendiri, fieldManajerCmn);
    void* rock = bacaPtr(state, fieldRock);
    void* charge = bacaPtr(rock, fieldCharge);
    void* hh = bacaPtr(sendiri, fieldCmnHh);
    void* config = bacaPtr(hh, fieldHhRock);
    void* chargeConfig = bacaPtr(config, fieldConfigCharge);
    if (charge && chargeConfig)
        tulisInt(charge, fieldStep, bacaInt(chargeConfig, fieldStepPenuh));

    return asli(sendiri, metode);
}

bool cariFieldSenjata() {
    void* cmn = kelasDari("Player__State__Cmn");
    void* senjata = kelasDari("Player__State__Std__Action__Hh__Weapon");
    void* manajerState = kelasDari("Player__State");
    return cariField(manajerState, "m", fieldManajerState) &&
           cariField(senjata, "m", fieldManajerSenjata) &&
           cariField(senjata, "cmn", fieldSenjataCmn) && cariField(cmn, "m", fieldManajerCmn) &&
           cariField(cmn, "chr_Hh", fieldCmnHh) &&
           cariField(kelasField(fieldManajerSenjata), "__state", fieldState) &&
           cariField(kelasField(fieldState), "combo", fieldCombo) &&
           cariField(kelasField(fieldState), "_rockman__Rock_Buster", fieldRock) &&
           cariField(kelasField(fieldRock), "charge", fieldCharge) &&
           cariField(kelasField(fieldCharge), "step", fieldStep) &&
           cariField(kelasField(fieldCmnHh), "_rockman__Rock_Buster", fieldHhRock) &&
           cariField(kelasField(fieldHhRock), "charge", fieldConfigCharge) &&
           cariField(kelasField(fieldConfigCharge), "step__Full", fieldStepPenuh);
}

} // namespace

void pasangHookSenjata() {
    if (!cariFieldSenjata()) {
        LOGE("Field senjata tidak ditemukan; senjata tetap vanilla");
        return;
    }
    LOGI("Field senjata siap");

    if (!target(T_ROCK_ACTION).addr || !target(T_ROCK_END_DIR).addr)
        LOGE("Method tembak ulang buster tidak ditemukan; buster berdiri tetap vanilla");

    pasangHook(target(T_STATE_UPDATE), reinterpret_cast<void*>(hookUpdateState));
    pasangHook(target(T_GUN_END), reinterpret_cast<void*>(hookAkhirPistol));
    pasangHook(target(T_GUN_ACTION_END), reinterpret_cast<void*>(hookAkhirAksiPistol));
    pasangHook(target(T_GUN_UPDATE), reinterpret_cast<void*>(hookUpdatePistol));
    pasangHook(target(T_ROCK_UPDATE), reinterpret_cast<void*>(hookUpdateBuster));
    pasangHook(target(T_ROCK_ACTION_END), reinterpret_cast<void*>(hookAkhirAksiBuster));
    pasangHook(target(T_ROCK_END_UPDATE), reinterpret_cast<void*>(hookUpdateAkhirBuster));
    pasangHook(target(T_ROCK_END_TO_ACTION), reinterpret_cast<void*>(hookAkhirKeAksiBuster));
    pasangHook(target(T_ROCK_ID), reinterpret_cast<void*>(hookIdBuster));
}
