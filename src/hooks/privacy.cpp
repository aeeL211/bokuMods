#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Setiap baris detail akun menanyakan ini sebelum memutuskan apa yang ditampilkan.
// Menjawab ya membuat pilihan "sembunyikan" milik pemilik tidak menggantikan
// nilainya.
bool hookManajer(void*, int, void*, bool, void*) { return true; }

// Layar teman, blacklist, tambah teman, dan verifikasi membawa salinan
// pengecekan yang sama, jadi semuanya dijawab dengan cara yang sama.
bool hookBaris(void*, int, void*) { return true; }

// Objek pengguna dan akun tamu menyimpan email serta nilai privasi yang
// menentukan boleh-tidaknya email itu dikirim dalam bentuk yang sama, jadi satu
// pencari field cukup untuk keduanya.
struct FieldEmail {
    Field email;
    Field privasi;
    Field emailPrivasi;
    Field terlihat;
};

FieldEmail emailPengguna = {};
FieldEmail emailTamu = {};
Field fieldPilihPengguna = {};
Field fieldManajerTamu = {};
Field fieldDaftarTamu = {};
Field fieldItemTamu = {};
Field fieldUkuranTamu = {};
Field fieldKunciTamu = {};
Field fieldAkunTamu = {};
bool isiTercatat = false;

using FnSetel = void (*)(void*, void*, void*, void*, bool, void*);
using FnSetelLokal = void (*)(void*, void*, void*, void*);
using FnUmum = void (*)(void*, void*, bool, void*, void*, void*, void*, void*, void*);
using FnPrivasi = void (*)(void*, void*, int, void*, void*);

// Kata yang dipakai game sendiri untuk sebuah nilai privasi email, jadi barisnya
// terbaca seperti layar pengaturan dan bukan kata buatan sendiri.
void* labelPrivasi(int terlihat) {
    auto fn = reinterpret_cast<void* (*)(int, void*)>(target(T_PRIVACY_LABEL).addr);
    return fn ? fn(terlihat, target(T_PRIVACY_LABEL).method) : nullptr;
}

// Email milik akun lain hanya ikut terkirim bila pemiliknya mengizinkan, jadi
// email yang disembunyikan membuat barisnya kosong. Label privasi milik pemilik
// ditampilkan sebagai gantinya.
void isiEmail(void* pemilik, const FieldEmail& bidang) {
    if (!pemilik)
        return;
    void* email = bacaPtr(pemilik, bidang.email);
    if (email && panjangString(email) > 0)
        return;
    void* privasi = bacaPtr(pemilik, bidang.privasi);
    void* emailPrivasi = privasi ? bacaPtr(privasi, bidang.emailPrivasi) : nullptr;
    if (!emailPrivasi)
        return;
    void* label = labelPrivasi(bacaInt(emailPrivasi, bidang.terlihat));
    if (!label)
        return;
    setelRef(pemilik, bidang.email, label);
    if (isiTercatat)
        return;
    isiTercatat = true;
    char teks[64];
    if (keUtf8(label, teks, sizeof(teks)))
        LOGI("Label privasi email: %s", teks);
}

bool cariFieldEmail(FieldEmail& bidang, void* kelas) {
    if (!kelas)
        return false;
    return cariField(kelas, "email", bidang.email) && cariField(kelas, "privacy", bidang.privasi) &&
           cariField(kelasField(bidang.privasi), "email", bidang.emailPrivasi) &&
           cariField(kelasField(bidang.emailPrivasi), "visible", bidang.terlihat);
}

void isiEmailPengguna(void* refPengguna) {
    isiEmail(refPengguna ? *reinterpret_cast<void**>(refPengguna) : nullptr, emailPengguna);
}

void hookSetel(void* sendiri, void* refPengguna, void* uid, void* dokumen, bool timpa,
               void* metode) {
    auto asli = reinterpret_cast<FnSetel>(target(T_USER_SET).orig);
    if (asli)
        asli(sendiri, refPengguna, uid, dokumen, timpa, metode);
    isiEmailPengguna(refPengguna);
}

// Ketiga penyetel lokal hanya berbeda pada tipe argumen keduanya.
void setelLokal(TargetId id, void* sendiri, void* refPengguna, void* data, void* metode) {
    auto asli = reinterpret_cast<FnSetelLokal>(target(id).orig);
    if (asli)
        asli(sendiri, refPengguna, data, metode);
    isiEmailPengguna(refPengguna);
}

void hookSetelString(void* sendiri, void* refPengguna, void* data, void* metode) {
    setelLokal(T_USER_SET_LOCAL, sendiri, refPengguna, data, metode);
}

void hookSetelArray(void* sendiri, void* refPengguna, void* data, void* metode) {
    setelLokal(T_USER_SET_LOCAL_ARRAY, sendiri, refPengguna, data, metode);
}

void hookSetelList(void* sendiri, void* refPengguna, void* data, void* metode) {
    setelLokal(T_USER_SET_LOCAL_LIST, sendiri, refPengguna, data, metode);
}

// Manajer tidak menyimpan akun sendiri: setiap tamu di daftar room membawa satu,
// dengan kunci berupa teks yang sama dengan nama di paket profil. Akunnya karena
// itu dicari di daftar tersebut, dan kunci dari luar room tidak menyisakan apa
// pun untuk diisi.
void* akunTamu(void* sendiri, void* pengguna) {
    void* manajer = bacaPtr(sendiri, fieldManajerTamu);
    void* daftarTamu = bacaPtr(manajer, fieldDaftarTamu);
    void* item = bacaPtr(daftarTamu, fieldItemTamu);
    if (!item)
        return nullptr;

    int32_t jumlah = bacaInt(daftarTamu, fieldUkuranTamu);
    int32_t kapasitas = panjangArray(item);
    if (jumlah > kapasitas)
        jumlah = kapasitas;

    char* data = dataArray(item);
    for (int32_t i = 0; i < jumlah; i++) {
        void* tamu = *reinterpret_cast<void**>(data + i * sizeof(void*));
        if (!tamu || !samaString(bacaPtr(tamu, fieldKunciTamu), pengguna))
            continue;
        return bacaPtr(tamu, fieldAkunTamu);
    }
    return nullptr;
}

// Paket profil membangun akun tamu dan pesan privasi bisa mengganti emailnya
// lagi, jadi kedua jalur berakhir pada pengisian yang sama.
void hookUmum(void* sendiri, void* pengguna, bool login, void* uid, void* email, void* tanggal,
              void* potret, void* perkenalan, void* metode) {
    auto asli = reinterpret_cast<FnUmum>(target(T_GUEST_COMMON).orig);
    if (asli)
        asli(sendiri, pengguna, login, uid, email, tanggal, potret, perkenalan, metode);
    isiEmail(akunTamu(sendiri, pengguna), emailTamu);
}

void hookEmailPrivasi(void* sendiri, void* pengguna, int terlihat, void* email, void* metode) {
    auto asli = reinterpret_cast<FnPrivasi>(target(T_GUEST_PRIVACY_EMAIL).orig);
    if (asli)
        asli(sendiri, pengguna, terlihat, email, metode);
    isiEmail(akunTamu(sendiri, pengguna), emailTamu);
}

bool cariFieldTamu() {
    void* kelasManajerTamu = kelasDari("Guest__Manager");
    if (!cariField(kelasManajerTamu, "a", fieldManajerTamu))
        return false;
    return cariField(kelasField(fieldManajerTamu), "guests", fieldDaftarTamu) &&
           cariField(kelasField(fieldDaftarTamu), "_items", fieldItemTamu) &&
           cariField(kelasField(fieldDaftarTamu), "_size", fieldUkuranTamu) &&
           cariField(kelasElemenField(fieldItemTamu), "key", fieldKunciTamu) &&
           cariField(kelasElemenField(fieldItemTamu), "account", fieldAkunTamu) &&
           cariFieldEmail(emailTamu, kelasField(fieldAkunTamu));
}

bool cariFieldPengguna() {
    void* manajer = kelasDari("Net__Account__Manager");
    return cariField(manajer, "user_Select", fieldPilihPengguna) &&
           cariFieldEmail(emailPengguna, kelasField(fieldPilihPengguna));
}

} // namespace

void pasangHookPrivasi() {
    if (cariFieldPengguna())
        LOGI("Field privasi siap");
    else
        LOGE("Field privasi tidak ditemukan; email yang disembunyikan tetap kosong");
    if (!cariFieldTamu())
        LOGE("Field akun tamu tidak ditemukan; email di dalam room tetap kosong");

    const TargetId penyetel[] = {T_USER_SET, T_USER_SET_LOCAL, T_USER_SET_LOCAL_ARRAY,
                                 T_USER_SET_LOCAL_LIST};
    int ditemukan = 0;
    for (TargetId id : penyetel)
        ditemukan += target(id).addr ? 1 : 0;
    LOGI("Penyetel email: %d dari 4", ditemukan);

    pasangHook(target(T_PRIVACY_MANAGER), reinterpret_cast<void*>(hookManajer));
    pasangHook(target(T_PRIVACY_BLACKLIST), reinterpret_cast<void*>(hookBaris));
    pasangHook(target(T_PRIVACY_ADD_FRIEND), reinterpret_cast<void*>(hookBaris));
    pasangHook(target(T_PRIVACY_FRIEND_PROFILE), reinterpret_cast<void*>(hookBaris));
    pasangHook(target(T_PRIVACY_VERIFY), reinterpret_cast<void*>(hookBaris));
    pasangHook(target(T_PRIVACY_VERIFY_EMAIL), reinterpret_cast<void*>(hookBaris));
    pasangHook(target(T_USER_SET), reinterpret_cast<void*>(hookSetel));
    pasangHook(target(T_USER_SET_LOCAL), reinterpret_cast<void*>(hookSetelString));
    pasangHook(target(T_USER_SET_LOCAL_ARRAY), reinterpret_cast<void*>(hookSetelArray));
    pasangHook(target(T_USER_SET_LOCAL_LIST), reinterpret_cast<void*>(hookSetelList));
    pasangHook(target(T_GUEST_COMMON), reinterpret_cast<void*>(hookUmum));
    pasangHook(target(T_GUEST_PRIVACY_EMAIL), reinterpret_cast<void*>(hookEmailPrivasi));
}
