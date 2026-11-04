#include "hooks.h"

#include <cstdint>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Entri lemparan menskalakan kecepatannya dengan nilai ini, jadi ujung atas
// rentangnya adalah lemparan terkuat yang bisa dibuat kotoran.
constexpr float kKekuatanPenuh = 1.0f;

// Lemparan bawaan mengisi charge selama tombol ditekan dan hanya melepas saat
// tombol dilepas, jadi tombol yang ditahan butuh pemicunya sendiri. Jendelanya
// sama dengan pengulangan pistol dan buster.
constexpr long long kIramaLemparMs = 180;

Field fieldManajer = {};
Field fieldStateManajer = {};
Field fieldLemparManajer = {};
Field fieldCharge = {};
Field fieldChargeAktif = {};

// Setiap pemain melempar dari objek state-nya sendiri, jadi irama, arah lempar
// terakhir, dan frame charge mengikuti manajer pemain.
struct Lemparan {
    void* manajer;
    long long terakhirMs;
    bool keAtas;
    int frameCharge;
};
constexpr int kJumlahLemparan = 32;
Lemparan daftarLemparan[kJumlahLemparan] = {};
int lemparanBerikut = 0;

long long sekarangMs() {
    timespec sekarang = {};
    clock_gettime(CLOCK_MONOTONIC, &sekarang);
    return static_cast<long long>(sekarang.tv_sec) * 1000 + sekarang.tv_nsec / 1000000;
}

Lemparan& lemparanUntuk(void* manajer) {
    Lemparan* slotKosong = nullptr;
    for (Lemparan& lemparan : daftarLemparan) {
        if (lemparan.manajer == manajer)
            return lemparan;
        if (!lemparan.manajer && !slotKosong)
            slotKosong = &lemparan;
    }

    Lemparan& slot = slotKosong ? *slotKosong : daftarLemparan[lemparanBerikut];
    lemparanBerikut = (lemparanBerikut + 1) % kJumlahLemparan;
    slot = {};
    slot.manajer = manajer;
    return slot;
}

// Bernilai benar paling sering sekali per jendela, jadi tombol yang ditahan
// melempar dengan laju tetap.
bool iramaSiap(Lemparan& lemparan) {
    long long sekarang = sekarangMs();
    if (sekarang - lemparan.terakhirMs < kIramaLemparMs)
        return false;
    lemparan.terakhirMs = sekarang;
    return true;
}

// Mencatat arah lemparan yang sedang mulai dan memasang iramanya, jadi
// pengulangan pertama menunggu satu jendela daripada menumpuk pada lemparan
// bawaan.
void tandaiLemparan(void* sendiri, bool keAtas) {
    void* manajer = bacaPtr(sendiri, fieldManajer);
    if (!manajer)
        return;
    Lemparan& lemparan = lemparanUntuk(manajer);
    lemparan.keAtas = keAtas;
    lemparan.terakhirMs = sekarangMs();
    lemparan.frameCharge = 0;
}

// Charge membuat penangan pelepasan tetap siap, jadi charge dibuang setelah
// lemparan sudah meninggalkan tangan dan pelepasan akan menambah lemparan kedua.
void kosongkanCharge(void* manajer) {
    void* state = bacaPtr(manajer, fieldStateManajer);
    void* dataLempar = bacaPtr(state, fieldLemparManajer);
    void* charge = bacaPtr(dataLempar, fieldCharge);
    if (charge)
        tulisBool(charge, fieldChargeAktif, false);
}

// Lemparan mandiri memanggil satu entri yang menerima kekuatan, jadi entri yang
// sama dipakai ulang untuk pengulangan. Method info tersembunyinya adalah yang
// disimpan scan untuk entri itu, karena badan yang membacanya tidak boleh
// melihat nilai null.
void lemparSekarang(void* sendiri, bool keAtas) {
    Target& entri = target(keAtas ? T_FECES_ACTION_UP : T_FECES_ACTION_STD);
    if (entri.addr)
        reinterpret_cast<void (*)(void*, float, void*)>(entri.addr)(sendiri, kKekuatanPenuh,
                                                                    entri.method);
}

// Charge lurus dimasuki dari setiap bidikan, dan charge melihat ke atas
// menggantikannya saat pemain membidik ke atas, jadi entri charge terakhir
// adalah arah yang harus diulang tombol yang ditahan. Hanya perubahan arah yang
// memasang ulang hitungan frame, jadi charge baru sempat menyelesaikan
// bidikannya tanpa menahan entri yang berulang.
void setelArahCharge(void* sendiri, bool keAtas) {
    void* manajer = bacaPtr(sendiri, fieldManajer);
    if (!manajer)
        return;
    Lemparan& lemparan = lemparanUntuk(manajer);
    if (lemparan.keAtas != keAtas)
        lemparan.frameCharge = 0;
    lemparan.keAtas = keAtas;
}

void hookChargeStd(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_STD).orig);
    if (!asli)
        return;
    asli(sendiri, metode);
    setelArahCharge(sendiri, false);
}

// Charge melihat ke atas hanya punya satu pemanggil, yaitu sakelar input yang
// berjalan selama pemain membidik ke atas saat charge.
void hookChargeUp(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_UP).orig);
    if (!asli)
        return;
    asli(sendiri, metode);
    setelArahCharge(sendiri, true);
}

// Id tangan kosong di akhir lemparan mengeluarkan kotoran dari tangan. Menahan
// perubahan itu membuat kotorannya tetap di tempat, jadi kotoran yang sama bisa
// terbang lagi, dan kekuatannya dipaksa supaya setiap lemparan terkuat.
void hookAksiStd(void* sendiri, float, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, float, void*)>(target(T_FECES_ACTION_STD).orig);
    if (!asli)
        return;
    tandaiLemparan(sendiri, false);
    setelTahanTangan(true);
    asli(sendiri, kKekuatanPenuh, metode);
    setelTahanTangan(false);
}

// Lemparan melihat ke atas butuh perlakuan yang sama.
void hookAksiUp(void* sendiri, float, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, float, void*)>(target(T_FECES_ACTION_UP).orig);
    if (!asli)
        return;
    tandaiLemparan(sendiri, true);
    setelTahanTangan(true);
    asli(sendiri, kKekuatanPenuh, metode);
    setelTahanTangan(false);
}

// Berjalan setiap frame selama aksi lempar ada di layar, dan jalur bawaan
// mengakhiri aksi itu begitu sprite mencapai step terakhirnya. Memulai lemparan
// setelahnya melempar lagi selama tombol ditahan.
void hookUpdateLempar(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_UPDATE).orig);
    if (!asli)
        return;
    asli(sendiri, metode);

    void* manajer = bacaPtr(sendiri, fieldManajer);
    if (!manajer || !tombolAksiDitekan(manajer))
        return;
    Lemparan& lemparan = lemparanUntuk(manajer);
    if (iramaSiap(lemparan))
        lemparSekarang(sendiri, lemparan.keAtas);
}

// Berjalan setiap frame state charge. Tombol yang ditahan langsung melempar
// daripada menunggu pelepasan, jadi lemparan pertama saat tombol ditahan jatuh
// sama seperti setiap pengulangan. Sakelar bidikan berjalan pada lintasan input
// berikutnya, jadi frame charge pertama hanya menghitung dan lemparan mulai
// satu frame kemudian dengan arah yang sudah mantap.
void hookUpdateCharge(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_FECES_CHARGE_UPDATE).orig);
    if (!asli)
        return;
    asli(sendiri, metode);

    void* manajer = bacaPtr(sendiri, fieldManajer);
    if (!manajer || !tombolAksiDitekan(manajer))
        return;
    Lemparan& lemparan = lemparanUntuk(manajer);
    if (lemparan.frameCharge == 0) {
        lemparan.frameCharge = 1;
        return;
    }
    if (!iramaSiap(lemparan))
        return;
    kosongkanCharge(manajer);
    lemparSekarang(sendiri, lemparan.keAtas);
}

bool cariFieldKotoran() {
    void* manajer = kelasDari("Player__Manager");
    return cariField(kelasDari("Player__State__Std__Action__Hh__Nature"), "m", fieldManajer) &&
           cariField(manajer, "__state", fieldStateManajer) &&
           cariField(kelasField(fieldStateManajer), "_throw", fieldLemparManajer) &&
           cariField(kelasField(fieldLemparManajer), "charge", fieldCharge) &&
           cariField(kelasField(fieldCharge), "active", fieldChargeAktif);
}

} // namespace

void pasangHookKotoran() {
    if (!cariFieldKotoran()) {
        LOGE("Field kotoran tidak ditemukan; lemparan tetap vanilla");
        return;
    }
    LOGI("Field kotoran siap");

    pasangHook(target(T_FECES_CHARGE_UPDATE), reinterpret_cast<void*>(hookUpdateCharge));
    pasangHook(target(T_FECES_CHARGE_STD), reinterpret_cast<void*>(hookChargeStd));
    pasangHook(target(T_FECES_CHARGE_UP), reinterpret_cast<void*>(hookChargeUp));
    pasangHook(target(T_FECES_UPDATE), reinterpret_cast<void*>(hookUpdateLempar));
    pasangHook(target(T_FECES_ACTION_STD), reinterpret_cast<void*>(hookAksiStd));
    pasangHook(target(T_FECES_ACTION_UP), reinterpret_cast<void*>(hookAksiUp));
}
