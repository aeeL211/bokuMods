#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

using FnTerima = void (*)(void*, void*, void*, void*, void*);
using FnChatRoom = void (*)(void*, void*, void*, void*);
using FnKunci = void* (*)(void*, void*);

// Setiap baris chat digambar ulang pada tiap pesan, jadi isi yang panjang
// dipotong pada sekian unit UTF-16 ini daripada disalin utuh.
constexpr size_t kBatasKarakterBaris = 512;
constexpr size_t kBatasByteNama = 64;
constexpr size_t kBatasByteLog = 160;

Field fieldManajer = {};
Field fieldChat = {};
Field fieldPemain = {};
Field fieldDaftarTamu = {};
Field fieldItemTamu = {};
Field fieldUkuranTamu = {};
Field fieldKunciTamu = {};
Field fieldAkunTamu = {};
Field fieldNamaPengguna = {};
bool chatSiap = false;

// Payload bisa membawa byte kontrol, yang akan membelah satu baris log menjadi
// dua, jadi byte itu diratakan menjadi spasi.
void ratakan(char* teks) {
    for (; *teks; teks++) {
        if (static_cast<unsigned char>(*teks) < 0x20)
            *teks = ' ';
    }
}

const char* utf8Dari(void* teks, char* keluaran, size_t ukuran) {
    if (!keUtf8(teks, keluaran, ukuran)) {
        keluaran[0] = '\0';
        return keluaran;
    }
    ratakan(keluaran);
    return keluaran;
}

void catatPesanPribadi(void* pengguna, void* penerima, void* isi) {
    char teksPengguna[kBatasByteNama];
    char teksPenerima[kBatasByteNama];
    char teksIsi[kBatasByteLog];
    LOGI("Pesan pribadi: %s -> %s: %s", utf8Dari(pengguna, teksPengguna, sizeof(teksPengguna)),
         utf8Dari(penerima, teksPenerima, sizeof(teksPenerima)),
         utf8Dari(isi, teksIsi, sizeof(teksIsi)));
}

// Pesan pribadi membawa kunci, bukan nama tampilan. Daftar tamu memakai kunci
// yang sama, jadi namanya dicari di sana; kunci yang tidak dikenal berarti pesan
// itu datang dari luar room ini dan tidak bisa diberi label.
void* namaTamu(void* sendiri, void* kunci) {
    void* manajer = bacaPtr(sendiri, fieldManajer);
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
        if (!tamu || !samaString(bacaPtr(tamu, fieldKunciTamu), kunci))
            continue;

        void* akun = bacaPtr(tamu, fieldAkunTamu);
        void* nama = bacaPtr(akun, fieldNamaPengguna);
        if (nama && panjangString(nama) > 0)
            return nama;
    }
    return nullptr;
}

// Room chat sudah memberi label pengirim, jadi hanya penerima yang ditaruh di
// depan isi. Prefiksnya ASCII dan sisanya tetap UTF-16.
void* barisPribadi(void* nama, void* teks) {
    const uint16_t* isi = karakterString(teks);
    int32_t panjangIsi = panjangString(teks);
    if (!isi || panjangIsi <= 0)
        return nullptr;

    const uint16_t* karakterNama = nama ? karakterString(nama) : nullptr;
    int32_t panjangNama = karakterNama ? panjangString(nama) : 0;
    bool bernama = panjangNama > 0;

    uint16_t baris[kBatasKarakterBaris];
    size_t terpakai = 0;
    for (const char* p = bernama ? "[PM -> " : "[PM] "; *p && terpakai < kBatasKarakterBaris; p++)
        baris[terpakai++] = static_cast<uint16_t>(*p);
    for (int32_t i = 0; bernama && i < panjangNama && terpakai < kBatasKarakterBaris; i++)
        baris[terpakai++] = karakterNama[i];
    if (bernama) {
        for (const char* p = "] "; *p && terpakai < kBatasKarakterBaris; p++)
            baris[terpakai++] = static_cast<uint16_t>(*p);
    }
    for (int32_t i = 0; i < panjangIsi && terpakai < kBatasKarakterBaris; i++)
        baris[terpakai++] = isi[i];
    return stringUtf16Baru(baris, static_cast<int32_t>(terpakai));
}

// Pengecekan penerima di dalam game membandingkan nilai persis ini, jadi
// panggilan yang sama memutuskan apakah sebuah pesan sudah punya jendela
// sendiri.
void* namaLokal(void* sendiri) {
    void* manajer = bacaPtr(sendiri, fieldManajer);
    void* pemain = bacaPtr(manajer, fieldPemain);
    auto kunci = reinterpret_cast<FnKunci>(target(T_PLAYER_KEY).addr);
    if (!pemain || !kunci)
        return nullptr;
    return kunci(pemain, target(T_PLAYER_KEY).method);
}

// Pesan pribadi disiarkan ke setiap klien dan hanya pengecekan penerima yang
// membuangnya, jadi teksnya sudah ada di sini. Pesan itu dicatat, lalu diputar
// ulang ke room chat atas nama pengirim dengan penerima di prefiks dan tanpa
// paket sendiri. Room chat menyelesaikan nama itu dari daftar tamu, jadi
// pengirim yang tidak ada di room ini dilewati daripada diserahkan ke game.
void hookTerima(void* sendiri, void* penerima, void* pengguna, void* teks, void* metode) {
    catatPesanPribadi(pengguna, penerima, teks);

    auto asli = reinterpret_cast<FnTerima>(target(T_GUEST_MSG_RECEIVE).orig);
    if (asli)
        asli(sendiri, penerima, pengguna, teks, metode);

    auto chatRoom = reinterpret_cast<FnChatRoom>(target(T_CHAT_MSG_RECEIVE).addr);
    if (!chatSiap || !chatRoom)
        return;

    void* milik = namaLokal(sendiri);
    if (milik && samaString(milik, penerima))
        return;

    void* pengirim = namaTamu(sendiri, pengguna);
    if (!pengirim) {
        char teksKunci[kBatasByteNama];
        LOGI("Pesan pribadi dilewati, pengirim tidak ada di room: %s",
             utf8Dari(pengguna, teksKunci, sizeof(teksKunci)));
        return;
    }

    void* chat = bacaPtr(sendiri, fieldChat);
    void* baris = barisPribadi(namaTamu(sendiri, penerima), teks);
    if (chat && baris)
        chatRoom(chat, pengguna, baris, target(T_CHAT_MSG_RECEIVE).method);
}

bool cariFieldTamu() {
    return cariField(kelasField(fieldManajer), "guests", fieldDaftarTamu) &&
           cariField(kelasField(fieldDaftarTamu), "_items", fieldItemTamu) &&
           cariField(kelasField(fieldDaftarTamu), "_size", fieldUkuranTamu) &&
           cariField(kelasElemenField(fieldItemTamu), "key", fieldKunciTamu) &&
           cariField(kelasElemenField(fieldItemTamu), "account", fieldAkunTamu) &&
           cariField(kelasField(fieldAkunTamu), "user_Name", fieldNamaPengguna);
}

} // namespace

void pasangHookChat() {
    void* kelasChat = kelasDari("Net__Multi__Guest__Chat");
    chatSiap = cariField(kelasChat, "chat", fieldChat);
    bool siapNamaSendiri = cariField(kelasChat, "a", fieldManajer) &&
                           cariField(kelasField(fieldManajer), "player", fieldPemain) &&
                           target(T_PLAYER_KEY).addr;
    bool siapTamu = cariFieldTamu();

    if (chatSiap && siapNamaSendiri)
        LOGI("Field chat siap");
    else if (!chatSiap)
        LOGE("Field chat tidak ditemukan; pesan pribadi hanya masuk log");
    else
        LOGE("Nama sendiri tidak ditemukan; pesan untuk pemain ini muncul dua kali");

    if (!siapTamu)
        LOGE("Nama tamu tidak ditemukan; pesan pribadi tampil tanpa nama penerima");

    pasangHook(target(T_GUEST_MSG_RECEIVE), reinterpret_cast<void*>(hookTerima));
}
