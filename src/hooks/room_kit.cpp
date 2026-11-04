#include "hooks.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"
#include "toast.h"

namespace {

using FnKirimChat = bool (*)(void*, void*, bool, void*);
using FnKirimInput = void (*)(void*, void*);
using FnKunci = void* (*)(void*, void*);
using FnTendang = void (*)(void*, void*, void*);
using FnBersihkan = void (*)(void*, void*);
using FnTambahTeks = void (*)(void*, int32_t, void*, int32_t, int32_t, bool, void*, void*);
using FnKirimHapus = void (*)(void*, void*, void*);

constexpr int kMaksPemain = 16;
constexpr size_t kUkuranBaris = 240;
constexpr size_t kKarakterKunci = 40;
constexpr double kJedaTendang = 0.25;
constexpr double kJendelaTendang = 8.0;

Field fieldManajerChat = {};
Field fieldInputChat = {};
Field fieldTeksInput = {};
Field fieldPesanChat = {};
Field fieldPengirimPesan = {};
Field fieldPengirimSendiri = {};
Field fieldPengirimLain = {};
Field fieldTeksChat = {};
Field fieldTeksSaya = {};
Field fieldTamuManajer = {};
Field fieldMultiManajer = {};
Field fieldPemainManajer = {};
Field fieldItemTamu = {};
Field fieldUkuranTamu = {};
Field fieldKunciTamu = {};
Field fieldTransferMulti = {};
Field fieldPipeTransfer = {};
Field fieldManajerTamu = {};
bool siap = false;

// Paket tendang diberi jeda satu per detak. Antrean hanya menyimpan teks kunci
// target yang diambil saat perintah berjalan, jadi tidak ada isinya yang
// menunjuk tamu, chat, atau objek pemain yang bisa dibebaskan room berikutnya
// atau frame berikutnya.
struct TendangSemua {
    bool aktif;
    double berikut;
    double akhir;
    int terkirim;
    int indeksBerikut;
    int jumlah;
    char kunci[kMaksPemain][kKarakterKunci];
};

TendangSemua tendangSemua = {};

struct Sesi {
    void* manajer;
    void* chat;
    void* multi;
    void* pemain;
    void* kunci;
    int32_t idPengirim;
    bool bisaJawab;
};

double sekarangDetik() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Teks jawaban berformat UTF-8, sementara tabel chat menyimpan UTF-16, jadi
// byte-nya didekode di sini supaya nama atau kunci non-ASCII tidak menjadi
// simbol rusak.
void langkahUtf8(const char* teks, size_t& i, uint32_t& kode) {
    unsigned char awal = static_cast<unsigned char>(teks[i]);
    int tambahan = 0;
    if (awal < 0x80) {
        kode = awal;
        i += 1;
        return;
    }
    if ((awal & 0xE0) == 0xC0) {
        kode = awal & 0x1F;
        tambahan = 1;
    } else if ((awal & 0xF0) == 0xE0) {
        kode = awal & 0x0F;
        tambahan = 2;
    } else if ((awal & 0xF8) == 0xF0) {
        kode = awal & 0x07;
        tambahan = 3;
    } else {
        i += 1;
        kode = 0xFFFD;
        return;
    }
    for (int k = 1; k <= tambahan; k++) {
        unsigned char lanjut = static_cast<unsigned char>(teks[i + k]);
        if ((lanjut & 0xC0) != 0x80) {
            i += 1;
            kode = 0xFFFD;
            return;
        }
        kode = (kode << 6) | (lanjut & 0x3F);
    }
    i += static_cast<size_t>(tambahan) + 1;
}

void* utf16Dari(const char* teks) {
    uint16_t buf[kUkuranBaris];
    size_t n = 0;
    for (size_t i = 0; teks[i] && n + 2 < kUkuranBaris;) {
        uint32_t kode = 0;
        langkahUtf8(teks, i, kode);
        if (kode >= 0x10000) {
            kode -= 0x10000;
            buf[n++] = static_cast<uint16_t>(0xD800 + (kode >> 10));
            buf[n++] = static_cast<uint16_t>(0xDC00 + (kode & 0x3FF));
        } else {
            buf[n++] = static_cast<uint16_t>(kode);
        }
    }
    return stringUtf16Baru(buf, static_cast<int32_t>(n));
}

void teksDari(void* teks, char* keluaran, size_t ukuran, const char* cadangan) {
    keluaran[0] = '\0';
    if (teks && keUtf8(teks, keluaran, ukuran) && keluaran[0])
        return;
    snprintf(keluaran, ukuran, "%s", cadangan);
}

void* kunciSendiri(void* pemain) {
    auto kunci = reinterpret_cast<FnKunci>(target(T_PLAYER_KEY).orig);
    if (!pemain || !kunci)
        return nullptr;
    return kunci(pemain, target(T_PLAYER_KEY).method);
}

void* pipeDari(void* multi) {
    return bacaPtr(bacaPtr(multi, fieldTransferMulti), fieldPipeTransfer);
}

// Room chat hanya menggambar baris bila id pengirimnya sama dengan
// _msg.sender.self atau _msg.sender.other. Baris dengan id self digambar di
// kanan, sisi yang disediakan game untuk pesan pemain sendiri, jadi jawaban
// ditambahkan dengan id "other" dan memakai tata letak kiri seperti pesan yang
// diterima. Nama yang dicetak game untuk pemain ini ikut terbawa, karena tata
// letak other memberi label bubble dari nama yang diberikan.
int32_t pengirimJawaban(Sesi& sesi) {
    int32_t lain =
        bacaInt(bacaPtr(bacaPtr(sesi.chat, fieldPesanChat), fieldPengirimPesan), fieldPengirimLain);
    return lain > 0 && lain != sesi.idPengirim ? lain : sesi.idPengirim;
}

void* namaJawaban(Sesi& sesi) {
    void* saya = bacaPtr(bacaPtr(sesi.chat, fieldTeksChat), fieldTeksSaya);
    if (saya && panjangString(saya) > 0)
        return saya;
    return sesi.kunci ? sesi.kunci : utf16Dari("");
}

// Tanpa objek pesan, jawaban jatuh ke toast daripada menjadi diam.
void jawab(Sesi& sesi, const char* teks) {
    auto tambah = reinterpret_cast<FnTambahTeks>(target(T_CHAT_MSG_ADD).addr);
    void* isi = sesi.bisaJawab && tambah ? utf16Dari(teks) : nullptr;
    void* nama = isi ? namaJawaban(sesi) : nullptr;
    if (!isi || !nama) {
        tampilkanToast(teks);
        return;
    }
    tambah(sesi.chat, pengirimJawaban(sesi), nama, -1, 0, false, isi,
           target(T_CHAT_MSG_ADD).method);
}

void* teksInput(void* chat) { return bacaPtr(bacaPtr(chat, fieldInputChat), fieldTeksInput); }

bool bukaSesi(void* chat, Sesi& sesi) {
    sesi.manajer = bacaPtr(chat, fieldManajerChat);
    sesi.chat = chat;
    sesi.pemain = bacaPtr(sesi.manajer, fieldPemainManajer);
    sesi.multi = bacaPtr(sesi.manajer, fieldMultiManajer);
    if (!sesi.pemain || !sesi.multi)
        return false;
    sesi.kunci = kunciSendiri(sesi.pemain);
    void* pesan = bacaPtr(chat, fieldPesanChat);
    sesi.bisaJawab = pesan && fieldPengirimPesan.field && fieldPengirimSendiri.field &&
                     target(T_CHAT_MSG_ADD).addr;
    sesi.idPengirim = sesi.bisaJawab ? bacaInt(pesan, fieldPengirimSendiri) : 0;
    return true;
}

// Tombol tendang bawaan mengirim dua paket: tendangan membuat tamu menampilkan
// pemberitahuan, dan penghapusan tamu mengeluarkannya dari setiap daftar.
// Keduanya membawa kunci pengirim, jadi penggantian yang sama membuat klien
// menerimanya sebagai paket host.
bool tendangKunci(void* multi, void* kunci) {
    void* pipe = pipeDari(multi);
    auto tendang = reinterpret_cast<FnTendang>(target(T_KICK_SEND).addr);
    auto hapus = reinterpret_cast<FnKirimHapus>(target(T_GUEST_REMOVE_SEND).addr);
    if (!kunci || !pipe || !tendang)
        return false;
    mulaiSpoof(multi);
    tendang(pipe, kunci, target(T_KICK_SEND).method);
    if (hapus)
        hapus(pipe, kunci, target(T_GUEST_REMOVE_SEND).method);
    hentiSpoof();
    return true;
}

void selesaiTendangSemua() {
    tendangSemua.aktif = false;
    LOGI("Tendang semua selesai: %d tamu", tendangSemua.terkirim);
    if (tendangSemua.terkirim == 0)
        return;
    char baris[kUkuranBaris];
    snprintf(baris, sizeof(baris), "Kickall: %d tamu ditendang", tendangSemua.terkirim);
    tampilkanToast(baris);
}

// Detak berjalan selama room terbuka, dan itulah yang mengatur laju pengurasan.
// Hanya pipe dan manajer yang dibaca di sini, dan keduanya berasal dari objek
// yang sedang dipakai frame ini, jadi antrean sisa pergantian room paling jauh
// hanya menembak orang asing.
void detakTendangSemua(void* sendiri) {
    if (!tendangSemua.aktif)
        return;
    double sekarang = sekarangDetik();
    if (tendangSemua.indeksBerikut >= tendangSemua.jumlah || sekarang > tendangSemua.akhir) {
        selesaiTendangSemua();
        return;
    }
    if (sekarang < tendangSemua.berikut)
        return;
    void* multi = bacaPtr(bacaPtr(sendiri, fieldManajerTamu), fieldMultiManajer);
    if (!multi) {
        LOGI("Tendang semua dihentikan: room sudah tidak ada");
        tendangSemua.aktif = false;
        return;
    }
    tendangSemua.berikut = sekarang + kJedaTendang;
    LOGI("Tendang semua: %d dari %d", tendangSemua.indeksBerikut + 1, tendangSemua.jumlah);
    void* kunci = utf16Dari(tendangSemua.kunci[tendangSemua.indeksBerikut]);
    tendangSemua.indeksBerikut++;
    if (tendangKunci(multi, kunci))
        tendangSemua.terkirim++;
    else
        LOGE("Tendang gagal: tamu tidak punya kunci atau pipe");
}

void hookUpdate(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_GUEST_UPDATE).orig);
    if (asli)
        asli(sendiri, metode);
    // Panggilan game yang keluar lewat jalur kirim akan melewati henti yang
    // berpasangan, dan penggantian yang bocor membuat setiap pencarian kunci
    // berikutnya dijawab dengan kunci host.
    hentiSpoof();
    detakTendangSemua(sendiri);
}

// Host terdaftar sebagai tamu biasa, dan satu frame tidak bisa membawa semua
// paket, jadi perintah ini hanya mengisi antrean dan update tamu mengirim satu
// tendangan per detak. Antrean hanya menyimpan teks kunci, tidak pernah pointer
// tamu atau chat, jadi isinya tetap sah saat room berubah di bawahnya.
void perintahTendangSemua(Sesi& sesi) {
    void* daftar = bacaPtr(sesi.manajer, fieldTamuManajer);
    void* item = bacaPtr(daftar, fieldItemTamu);
    int32_t jumlah = item ? bacaInt(daftar, fieldUkuranTamu) : 0;
    int32_t kapasitas = item ? panjangArray(item) : 0;
    if (jumlah > kapasitas)
        jumlah = kapasitas;

    void* host = kunciHostDari(sesi.multi);
    char* data = item ? dataArray(item) : nullptr;
    int terisi = 0;
    for (int32_t i = 0; data && i < jumlah && terisi < kMaksPemain; i++) {
        void* tamu = *reinterpret_cast<void**>(data + i * sizeof(void*));
        if (!tamu)
            continue;
        void* kunci = bacaPtr(tamu, fieldKunciTamu);
        if (samaString(kunci, sesi.kunci) || samaString(kunci, host))
            continue;
        char teks[kKarakterKunci];
        teksDari(kunci, teks, sizeof(teks), "");
        if (!teks[0])
            continue;
        snprintf(tendangSemua.kunci[terisi], kKarakterKunci, "%s", teks);
        LOGI("Antrean tendang semua %d: %s", terisi + 1, teks);
        terisi++;
    }
    LOGI("Tendang semua mulai: %d tamu dari %d slot", terisi, jumlah);
    if (terisi == 0) {
        jawab(sesi, "Tidak ada tamu untuk ditendang");
        return;
    }
    tendangSemua.aktif = true;
    tendangSemua.berikut = 0.0;
    tendangSemua.akhir = sekarangDetik() + kJendelaTendang;
    tendangSemua.terkirim = 0;
    tendangSemua.indeksBerikut = 0;
    tendangSemua.jumlah = terisi;
    jawab(sesi, "Menendang tamu satu per satu");
}

// Barisnya disalin tanpa garis miring di depan, jadi namanya dicocokkan tanpa
// garis miring. Apa pun yang bukan ASCII tercetak biasa tetap menjadi chat
// biasa.
bool teksPerintah(void* teks, char* keluaran, size_t ukuran) {
    const uint16_t* karakter = karakterString(teks);
    int32_t panjang = panjangString(teks);
    if (!karakter || panjang < 2 || karakter[0] != '/')
        return false;
    size_t n = 0;
    for (int32_t i = 1; i < panjang && n + 1 < ukuran; i++) {
        uint16_t kode = karakter[i];
        if (kode < 0x20 || kode > 0x7E)
            return false;
        keluaran[n++] = static_cast<char>(kode >= 'A' && kode <= 'Z' ? kode + 32 : kode);
    }
    keluaran[n] = '\0';
    return true;
}

// Satu-satunya perintah yang dikenal adalah /kick all; bentuk /kick lain
// dijawab dengan pemakaian, karena menendang satu pemain ada di tombol panel.
// Perintah yang dikenal dijawab lokal dan kotaknya dibersihkan dengan reset
// milik game, jadi baris yang diketik tidak pernah sampai ke jaringan.
bool tanganiPerintah(void* chat, void* teks) {
    char baris[kUkuranBaris];
    if (!siap || !teksPerintah(teks, baris, sizeof(baris)))
        return false;
    Sesi sesi;
    if (!bukaSesi(chat, sesi)) {
        LOGE("Sesi perintah tidak ada: pemain atau room tidak ditemukan");
        return false;
    }
    if (!strcmp(baris, "kick all")) {
        LOGI("Perintah: kick all");
        perintahTendangSemua(sesi);
    } else if (!strcmp(baris, "kick") || !strncmp(baris, "kick ", 5)) {
        LOGI("Perintah: %s", baris);
        jawab(sesi, "Pakai /kick all, atau tombol tendang di panel pemain");
    } else {
        return false;
    }
    auto bersihkan = reinterpret_cast<FnBersihkan>(target(T_CHAT_CLEAR).addr);
    if (bersihkan)
        bersihkan(chat, target(T_CHAT_CLEAR).method);
    return true;
}

// Validator menerima string yang sama dengan yang dipakai pengiriman, jadi
// perintah dijawab di sini dan pengirimannya dilewati dengan mengembalikan
// false.
bool hookKirim(void* sendiri, void* teks, bool suara, void* metode) {
    LOGI("Kirim chat: panjang=%d suara=%d", panjangString(teks), suara ? 1 : 0);
    if (tanganiPerintah(sendiri, teks))
        return false;
    auto asli = reinterpret_cast<FnKirimChat>(target(T_CHAT_SEND).orig);
    return asli ? asli(sendiri, teks, suara, metode) : false;
}

// Tombol kirim dan aksi keyboard sama-sama mendarat di sini dengan field yang
// sudah terisi, jadi perintahnya diambil sebelum validator berjalan sama sekali.
void hookTombolKirim(void* sendiri, void* metode) {
    void* teks = teksInput(sendiri);
    LOGI("Tombol kirim: panjang=%d", panjangString(teks));
    if (tanganiPerintah(sendiri, teks))
        return;
    auto asli = reinterpret_cast<FnKirimInput>(target(T_INPUT_SEND).orig);
    if (asli)
        asli(sendiri, metode);
}

bool cariFieldRoomKit() {
    if (!cariField(kelasDari("Net__Multi__Chat"), "a", fieldManajerChat))
        return false;
    if (!cariField(kelasDari("Net__Multi__Chat"), "_input_Field", fieldInputChat) ||
        !cariField(kelasField(fieldInputChat), "text", fieldTeksInput))
        return false;
    void* kelasManajer = kelasField(fieldManajerChat);
    if (!cariField(kelasManajer, "guests", fieldTamuManajer) ||
        !cariField(kelasManajer, "multi", fieldMultiManajer) ||
        !cariField(kelasManajer, "player", fieldPemainManajer))
        return false;

    void* kelasDaftar = kelasField(fieldTamuManajer);
    if (!cariField(kelasDaftar, "_items", fieldItemTamu) ||
        !cariField(kelasDaftar, "_size", fieldUkuranTamu))
        return false;
    if (!cariField(kelasElemenField(fieldItemTamu), "key", fieldKunciTamu))
        return false;
    if (!cariField(kelasField(fieldMultiManajer), "transfer", fieldTransferMulti))
        return false;
    if (!cariField(kelasField(fieldTransferMulti), "pipe", fieldPipeTransfer))
        return false;

    // Callback frame manajer tamu menggerakkan antrean /kick all, jadi pointer
    // manajernya dicari dengan cara yang sama seperti field lainnya.
    if (!cariField(kelasDari("Guest__Manager"), "a", fieldManajerTamu))
        return false;

    // Jawaban menurun sendiri: field yang tidak ada mengirim jawabannya ke
    // toast, bukan mematikan seluruh perintah.
    if (cariField(kelasDari("Net__Multi__Chat"), "_msg", fieldPesanChat) &&
        cariField(kelasField(fieldPesanChat), "sender", fieldPengirimPesan)) {
        cariField(kelasField(fieldPengirimPesan), "self", fieldPengirimSendiri);
        cariField(kelasField(fieldPengirimPesan), "other", fieldPengirimLain);
    }
    if (cariField(kelasDari("Net__Multi__Chat"), "_text", fieldTeksChat))
        cariField(kelasField(fieldTeksChat), "me", fieldTeksSaya);
    return true;
}

} // namespace

void pasangHookRoomKit() {
    // Kunci pemain dibaca lewat trampoline milik spoof.cpp, jadi hook itu harus
    // sudah terpasang sebelum RoomKit memakai hasilnya sebagai kunci pengirim.
    siap = cariFieldRoomKit() && target(T_CHAT_SEND).addr && target(T_CHAT_CLEAR).addr &&
           target(T_INPUT_SEND).addr && target(T_PLAYER_KEY).orig && target(T_KICK_SEND).addr &&
           target(T_HOST_KEY).addr && target(T_GUEST_UPDATE).addr &&
           target(T_CHAT_MSG_ADD).addr && target(T_GUEST_REMOVE_SEND).addr;
    if (!siap) {
        LOGE("Field RoomKit tidak ditemukan; perintah chat tidak aktif");
        return;
    }
    LOGI("Field RoomKit siap");
    pasangHook(target(T_CHAT_SEND), reinterpret_cast<void*>(hookKirim));
    pasangHook(target(T_INPUT_SEND), reinterpret_cast<void*>(hookTombolKirim));
    pasangHook(target(T_GUEST_UPDATE), reinterpret_cast<void*>(hookUpdate));
}
