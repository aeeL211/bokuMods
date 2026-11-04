#include "hooks.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
constexpr int kMaksToken = 8;
constexpr int kJumlahBagian = 3;
constexpr size_t kUkuranBaris = 240;
constexpr size_t kUkuranNama = 96;
constexpr size_t kUkuranKunci = 16;
constexpr double kJedaTendang = 0.25;
constexpr double kJendelaTendang = 8.0;
constexpr size_t kKarakterKunci = 40;

Field fieldManajerChat = {};
Field fieldInputChat = {};
Field fieldTeksInput = {};
Field fieldPesanChat = {};
Field fieldPengirimPesan = {};
Field fieldPengirimSendiri = {};
Field fieldPengirimLain = {};
Field fieldTeksChat = {};
Field fieldTeksSaya = {};
Field fieldUiChat = {};
Field fieldUkurChat = {};
Field fieldMaksBarisLabel = {};
Field fieldDaftarPesan = {};
Field fieldItemPesan = {};
Field fieldJumlahPesan = {};
Field fieldObjekPesan = {};
Field fieldLabelPesan = {};
Field fieldTamuManajer = {};
Field fieldMultiManajer = {};
Field fieldPemainManajer = {};
Field fieldItemTamu = {};
Field fieldUkuranTamu = {};
Field fieldKunciTamu = {};
Field fieldAkunTamu = {};
Field fieldNamaAkun = {};
Field fieldTransferMulti = {};
Field fieldPipeTransfer = {};
Field fieldManajerTamu = {};
bool siap = false;
bool barisSiap = false;

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

struct Slot {
    void* tamu;
};

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

// Teks jawaban berformat UTF-8, karena nama pemain diubah ke UTF-8 sebelum
// dimasukkan ke sebuah baris, sementara tabel chat menyimpan UTF-16. Menyalin
// byte-nya satu per satu mengubah setiap nama non-ASCII menjadi simbol rusak,
// jadi teksnya didekode di sini.
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

void* namaAkunDari(void* tamu) { return bacaPtr(bacaPtr(tamu, fieldAkunTamu), fieldNamaAkun); }

void* kunciSendiri(void* pemain) {
    auto kunci = reinterpret_cast<FnKunci>(target(T_PLAYER_KEY).orig);
    if (!pemain || !kunci)
        return nullptr;
    return kunci(pemain, target(T_PLAYER_KEY).method);
}

void* pipeDari(void* multi) {
    return bacaPtr(bacaPtr(multi, fieldTransferMulti), fieldPipeTransfer);
}

// Daftar tamu berisi pemain jarak jauh, jadi pemain lokal selalu slot satu dan
// nomornya tidak bergantung pada urutan daftar room yang kebetulan dipakai game.
// Tamu yang membawa kunci kita sendiri dilewati, kalau-kalau game juga
// mencantumkan pemain lokal di sana.
int kumpulkanPemain(void* chat, void* milikku, Slot* hasil) {
    void* manajer = bacaPtr(chat, fieldManajerChat);
    void* daftar = bacaPtr(manajer, fieldTamuManajer);
    void* item = bacaPtr(daftar, fieldItemTamu);
    int n = 0;
    hasil[n++].tamu = nullptr;
    if (!item)
        return n;

    int32_t jumlah = bacaInt(daftar, fieldUkuranTamu);
    int32_t kapasitas = panjangArray(item);
    if (jumlah > kapasitas)
        jumlah = kapasitas;

    char* data = dataArray(item);
    for (int32_t i = 0; i < jumlah && n < kMaksPemain; i++) {
        void* tamu = *reinterpret_cast<void**>(data + i * sizeof(void*));
        if (!tamu)
            continue;
        if (samaString(bacaPtr(tamu, fieldKunciTamu), milikku))
            continue;
        hasil[n++].tamu = tamu;
    }
    return n;
}

// Sebuah bubble adalah instance prefab, jadi ia mulai dengan batas baris milik
// prefab itu sendiri. Bubble yang baru ditambahi jawaban multi-baris adalah
// entri terakhir daftar pesan, dan menaikkan batasnya menjaga teks tidak
// terpotong menjadi satu baris. Daftar yang tidak ada atau bubble yang belum
// dibangun membiarkan batasnya apa adanya.
void angkatLabelPesan(void* chat) {
    if (!fieldLabelPesan.field)
        return;
    void* daftar = bacaPtr(chat, fieldDaftarPesan);
    void* item = bacaPtr(daftar, fieldItemPesan);
    int32_t jumlah = bacaInt(daftar, fieldJumlahPesan);
    int32_t kapasitas = panjangArray(item);
    if (!item || jumlah < 1 || jumlah > kapasitas)
        return;
    void* pesan = *reinterpret_cast<void**>(dataArray(item) + (jumlah - 1) * sizeof(void*));
    void* label = bacaPtr(bacaPtr(pesan, fieldObjekPesan), fieldLabelPesan);
    tulisInt(label, fieldMaksBarisLabel, 0);
}

// Room chat hanya menggambar baris bila id pengirimnya sama dengan
// _msg.sender.self atau _msg.sender.other. Baris dengan id self digambar di
// kanan, sisi yang disediakan game untuk pesan pemain sendiri, jadi jawaban
// ditambahkan dengan id "other" dan memakai tata letak kiri seperti pesan yang
// diterima. Nama yang dicetak game untuk pemain ini ikut terbawa, karena tata
// letak other memberi label bubble dari nama yang diberikan. Field yang tidak
// ada mempertahankan baris self. Bubble-nya satu label NGUI, dan NGUI memecah
// teksnya pada setiap newline, jadi satu isi bisa memuat beberapa baris. Tanpa
// objek pesan, jawaban jatuh ke toast daripada menjadi diam.
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

void jawab(Sesi& sesi, const char* teks) {
    auto tambah = reinterpret_cast<FnTambahTeks>(target(T_CHAT_MSG_ADD).addr);
    if (!sesi.bisaJawab || !tambah) {
        tampilkanToast(teks);
        return;
    }
    void* isi = utf16Dari(teks);
    void* nama = namaJawaban(sesi);
    if (!isi || !nama) {
        tampilkanToast(teks);
        return;
    }
    tambah(sesi.chat, pengirimJawaban(sesi), nama, -1, 0, false, isi,
           target(T_CHAT_MSG_ADD).method);
    if (strchr(teks, '\n'))
        angkatLabelPesan(sesi.chat);
}

// Jawaban perintah terbaca sebagai satu bubble selama baris-barisnya
// dikumpulkan dalam satu label, jadi baris digabung dengan newline di sini. Blok
// dipotong sebelum melewati buffer teks UTF-16 atau menjadi bubble yang lebih
// tinggi dari panel chat, jadi jawaban panjang menjadi beberapa bubble pendek
// daripada satu bubble tinggi.
struct Blok {
    char teks[kUkuranBaris];
    size_t panjang;
    int baris;
};

constexpr size_t kKarakterBlok = 228;
constexpr int kBarisBlok = 6;

void aturUlangBlok(Blok& blok) {
    blok.teks[0] = '\0';
    blok.panjang = 0;
    blok.baris = 0;
}

void siramBlok(Sesi& sesi, Blok& blok) {
    if (blok.baris)
        jawab(sesi, blok.teks);
    aturUlangBlok(blok);
}

void tambahBlok(Sesi& sesi, Blok& blok, const char* barisBaru) {
    size_t tambahan = strlen(barisBaru);
    if (tambahan >= kKarakterBlok) {
        siramBlok(sesi, blok);
        jawab(sesi, barisBaru);
        return;
    }
    size_t jeda = blok.baris ? 1 : 0;
    if (blok.baris >= kBarisBlok || blok.panjang + jeda + tambahan > kKarakterBlok)
        siramBlok(sesi, blok);
    if (blok.baris)
        blok.teks[blok.panjang++] = '\n';
    memcpy(blok.teks + blok.panjang, barisBaru, tambahan);
    blok.panjang += tambahan;
    blok.teks[blok.panjang] = '\0';
    blok.baris++;
}

// Tinggi bubble berasal dari label template yang diubah game ke ResizeHeight
// sebelum setiap pengukuran, dan NGUI berhenti menambah baris pada maxLineCount
// label itu. Menaikkan batasnya sekali membuat blok multi-baris menghitung
// setiap baris, jadi bubble tumbuh mengikuti teks daripada memotongnya.
void siapkanBarisChat(void* chat) {
    if (barisSiap || !chat)
        return;
    void* label = bacaPtr(bacaPtr(chat, fieldUiChat), fieldUkurChat);
    if (!label)
        return;
    barisSiap = true;
    int32_t batas = bacaInt(label, fieldMaksBarisLabel);
    if (batas == 0)
        return;
    tulisInt(label, fieldMaksBarisLabel, 0);
    LOGI("Batas baris chat dinaikkan: %d", batas);
}

void* teksInput(void* chat) { return bacaPtr(bacaPtr(chat, fieldInputChat), fieldTeksInput); }

bool bukaSesi(void* chat, Sesi& sesi) {
    void* manajer = bacaPtr(chat, fieldManajerChat);
    sesi.manajer = manajer;
    sesi.chat = chat;
    sesi.pemain = bacaPtr(manajer, fieldPemainManajer);
    sesi.multi = bacaPtr(manajer, fieldMultiManajer);
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
// menerimanya sebagai paket host. Tendangan hanya butuh kunci target dan pipe,
// jadi kunci yang diantrekan bisa diputar ulang nanti tanpa menyentuh objek tamu
// asalnya.
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

bool teksKunciTamu(void* tamu, char* keluaran) {
    keluaran[0] = '\0';
    teksDari(bacaPtr(tamu, fieldKunciTamu), keluaran, kKarakterKunci, "");
    return keluaran[0] != '\0';
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

int pecahArgumen(char* teks, char** keluaran, int maks) {
    int n = 0;
    char* p = teks;
    while (*p && n < maks) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        keluaran[n++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    return n;
}

// Argumen perintah adalah masukan pemain, jadi karakter sisa atau nilai yang
// meluap ditolak daripada diam-diam menjadi angka lain.
bool uraiInt(const char* teks, int& keluaran) {
    char* akhir = nullptr;
    errno = 0;
    long nilai = strtol(teks, &akhir, 10);
    if (errno != 0 || akhir == teks || *akhir != '\0' || nilai < INT32_MIN || nilai > INT32_MAX)
        return false;
    keluaran = static_cast<int>(nilai);
    return true;
}

using FnPenangan = void (*)(Sesi&, int, char**);

struct Perintah {
    const char* nama;
    int minArgumen;
    int maksArgumen;
    int bagian;
    FnPenangan jalankan;
    const char* pemakaian;
};

void perintahDaftar(Sesi&, int, char**);
void perintahTendang(Sesi&, int, char**);
void perintahTendangSemua(Sesi&, int, char**);
void perintahMenu(Sesi&, int, char**);
void perintahBantuan(Sesi&, int, char**);
void perintahStatus(Sesi&, int, char**);
void perintahLog(Sesi&, int, char**);

// Perintah dikelompokkan menurut pekerjaan pemain: aksi room, informasi, lalu
// bantuan. Urutannya juga urutan yang dipakai /help saat mencetak semua baris.
const Perintah daftarPerintah[] = {
    {"kick", 1, 1, 1, perintahTendang, "/kick <n> - tendang pemain nomor n"},
    {"kickall", 0, 0, 1, perintahTendangSemua, "/kickall - tendang semua tamu, host tetap"},
    {"list", 0, 0, 2, perintahDaftar, "/list - daftar pemain dan nomornya"},
    {"status", 0, 0, 2, perintahStatus, "/status - kunci sendiri, host, jumlah pemain"},
    {"log", 0, 0, 2, perintahLog, "/log - lokasi berkas log sesi ini"},
    {"menu", 0, 0, 3, perintahMenu, "/menu - ringkasan semua perintah"},
    {"help", 0, 1, 3, perintahBantuan, "/help [perintah] - penjelasan perintah"},
};

constexpr int kJumlahPerintah = sizeof(daftarPerintah) / sizeof(daftarPerintah[0]);

const char* judulBagian(int bagian) {
    switch (bagian) {
    case 1:
        return "Aksi room";
    case 2:
        return "Info";
    default:
        return "Bantuan";
    }
}

// Ringkasan satu bagian menempelkan nama perintahnya pada satu baris, jadi
// ketiga bagian tetap muat dalam satu bubble.
void tambahNamaPerintah(char* baris, size_t ukuran, int bagian) {
    size_t terpakai = strlen(baris);
    for (int i = 0; i < kJumlahPerintah; i++) {
        if (daftarPerintah[i].bagian != bagian)
            continue;
        int n = snprintf(baris + terpakai, ukuran - terpakai, " /%s", daftarPerintah[i].nama);
        if (n <= 0 || static_cast<size_t>(n) >= ukuran - terpakai)
            return;
        terpakai += static_cast<size_t>(n);
    }
}

void perintahDaftar(Sesi& sesi, int, char**) {
    Slot daftarSlot[kMaksPemain];
    int jumlah = kumpulkanPemain(sesi.chat, sesi.kunci, daftarSlot);
    void* host = kunciHostDari(sesi.multi);
    Blok blok;
    aturUlangBlok(blok);
    char nama[kUkuranNama];
    char baris[kUkuranBaris];
    for (int i = 0; i < jumlah; i++) {
        if (!daftarSlot[i].tamu) {
            snprintf(baris, sizeof(baris), "#%d kamu", i + 1);
        } else {
            teksDari(namaAkunDari(daftarSlot[i].tamu), nama, sizeof(nama), "?");
            bool adalahHost = samaString(bacaPtr(daftarSlot[i].tamu, fieldKunciTamu), host);
            snprintf(baris, sizeof(baris), "#%d %s%s", i + 1, nama, adalahHost ? " (host)" : "");
        }
        tambahBlok(sesi, blok, baris);
    }
    siramBlok(sesi, blok);
}

void perintahTendang(Sesi& sesi, int, char** argv) {
    Slot daftarSlot[kMaksPemain];
    int jumlah = kumpulkanPemain(sesi.chat, sesi.kunci, daftarSlot);
    int indeks = 0;
    if (!uraiInt(argv[0], indeks)) {
        jawab(sesi, "Nomor pemain harus angka");
        return;
    }
    if (indeks < 1 || indeks > jumlah) {
        jawab(sesi, "Pemain nomor itu tidak ada");
        return;
    }
    if (!daftarSlot[indeks - 1].tamu) {
        jawab(sesi, "Tidak bisa menendang diri sendiri");
        return;
    }
    if (samaString(bacaPtr(daftarSlot[indeks - 1].tamu, fieldKunciTamu),
                   kunciHostDari(sesi.multi))) {
        jawab(sesi, "Host tidak bisa ditendang");
        return;
    }
    if (!tendangKunci(sesi.multi, bacaPtr(daftarSlot[indeks - 1].tamu, fieldKunciTamu))) {
        jawab(sesi, "Tendangan gagal");
        return;
    }
    jawab(sesi, "Tendangan terkirim");
}

// Host terdaftar sebagai tamu biasa, dan satu frame tidak bisa membawa semua
// paket, jadi perintah ini hanya mengisi antrean dan update tamu mengirim satu
// tendangan per detak. Antrean hanya menyimpan teks kunci, tidak pernah pointer
// tamu atau chat, jadi isinya tetap sah saat room berubah di bawahnya.
void perintahTendangSemua(Sesi& sesi, int, char**) {
    Slot daftarSlot[kMaksPemain];
    int jumlah = kumpulkanPemain(sesi.chat, sesi.kunci, daftarSlot);
    void* host = kunciHostDari(sesi.multi);
    int terisi = 0;
    for (int i = 1; i < jumlah && terisi < kMaksPemain; i++) {
        char kunci[kKarakterKunci];
        if (!teksKunciTamu(daftarSlot[i].tamu, kunci))
            continue;
        if (samaString(bacaPtr(daftarSlot[i].tamu, fieldKunciTamu), host))
            continue;
        snprintf(tendangSemua.kunci[terisi], kKarakterKunci, "%s", kunci);
        LOGI("Antrean tendang semua %d: %s", terisi + 1, kunci);
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

void perintahStatus(Sesi& sesi, int, char**) {
    void* kunciHost = kunciHostDari(sesi.multi);
    char milik[kUkuranKunci];
    char host[kUkuranKunci];
    char baris[kUkuranBaris];
    Blok blok;
    aturUlangBlok(blok);
    teksDari(sesi.kunci, milik, sizeof(milik), "?");
    teksDari(kunciHost, host, sizeof(host), "?");
    snprintf(baris, sizeof(baris), "Kunci kamu: %s", milik);
    tambahBlok(sesi, blok, baris);
    snprintf(baris, sizeof(baris), "Kunci host: %s", host);
    tambahBlok(sesi, blok, baris);
    tambahBlok(sesi, blok,
               samaString(sesi.kunci, kunciHost) ? "Kamu host: ya" : "Kamu host: tidak");
    Slot daftarSlot[kMaksPemain];
    snprintf(baris, sizeof(baris), "Pemain: %d",
             kumpulkanPemain(sesi.chat, sesi.kunci, daftarSlot));
    tambahBlok(sesi, blok, baris);
    siramBlok(sesi, blok);
}

void perintahLog(Sesi& sesi, int, char**) {
    const char* path = pathFileLog();
    jawab(sesi, path ? path : "Berkas log belum dibuka");
}

void tulisRingkasan(Sesi& sesi) {
    Blok blok;
    aturUlangBlok(blok);
    char baris[kUkuranBaris];
    snprintf(baris, sizeof(baris), "RoomKit - %d perintah", kJumlahPerintah);
    tambahBlok(sesi, blok, baris);
    for (int bagian = 1; bagian <= kJumlahBagian; bagian++) {
        snprintf(baris, sizeof(baris), "%s:", judulBagian(bagian));
        tambahNamaPerintah(baris, sizeof(baris), bagian);
        tambahBlok(sesi, blok, baris);
    }
    siramBlok(sesi, blok);
}

// Semua perintah muat dalam satu bubble, jadi tidak ada halaman yang harus
// dibuka satu per satu.
void perintahMenu(Sesi& sesi, int, char**) { tulisRingkasan(sesi); }

void perintahBantuan(Sesi& sesi, int argc, char** argv) {
    if (argc == 0) {
        for (int bagian = 1; bagian <= kJumlahBagian; bagian++) {
            Blok blok;
            aturUlangBlok(blok);
            tambahBlok(sesi, blok, judulBagian(bagian));
            for (int i = 0; i < kJumlahPerintah; i++) {
                if (daftarPerintah[i].bagian == bagian)
                    tambahBlok(sesi, blok, daftarPerintah[i].pemakaian);
            }
            siramBlok(sesi, blok);
        }
        return;
    }
    for (int i = 0; i < kJumlahPerintah; i++) {
        if (strcmp(daftarPerintah[i].nama, argv[0]))
            continue;
        jawab(sesi, daftarPerintah[i].pemakaian);
        return;
    }
    jawab(sesi, "Perintah tidak dikenal");
}

// Hanya perintah yang dikenal yang diambil dari jaringan; apa pun selain itu
// yang dimulai dengan garis miring adalah chat biasa dan sampai ke room tanpa
// perubahan.
bool jalankanPerintah(Sesi& sesi, int argc, char** argv) {
    for (int i = 0; i < kJumlahPerintah; i++) {
        const Perintah& perintah = daftarPerintah[i];
        if (strcmp(perintah.nama, argv[0]))
            continue;
        LOGI("Perintah: %s", perintah.nama);
        int jumlah = argc - 1;
        if (jumlah < perintah.minArgumen || jumlah > perintah.maksArgumen) {
            jawab(sesi, perintah.pemakaian);
            return true;
        }
        perintah.jalankan(sesi, jumlah, argv + 1);
        return true;
    }
    return false;
}

// Barisnya disalin tanpa garis miring di depan, jadi tabel perintah dicocokkan
// berdasarkan nama saja. Apa pun yang bukan ASCII tercetak biasa tetap menjadi
// chat biasa.
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

// Perintah yang dikenal dijawab secara lokal dan kotaknya dibersihkan dengan
// reset milik game, jadi baris yang diketik tidak pernah sampai ke jaringan.
bool tanganiPerintah(void* chat, void* teks) {
    char baris[kUkuranBaris];
    if (!siap || !teksPerintah(teks, baris, sizeof(baris)))
        return false;
    siapkanBarisChat(chat);
    Sesi sesi;
    if (!bukaSesi(chat, sesi)) {
        LOGE("Sesi perintah tidak ada: pemain atau room tidak ditemukan");
        return false;
    }
    char* argv[kMaksToken];
    int argc = pecahArgumen(baris, argv, kMaksToken);
    if (argc < 1 || !jalankanPerintah(sesi, argc, argv))
        return false;
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

// Mencari field bubble tempat jawaban mendarat: daftar pesan, pesan terakhir,
// dan label miliknya. Field yang tidak ada hanya membiarkan batas baris prefab
// apa adanya, jadi perintahnya tetap berjalan.
void cariFieldLabelPesan() {
    if (!cariField(kelasDari("Net__Multi__Chat"), "msgs", fieldDaftarPesan))
        return;
    void* kelasDaftar = kelasField(fieldDaftarPesan);
    if (!cariField(kelasDaftar, "_items", fieldItemPesan) ||
        !cariField(kelasDaftar, "_size", fieldJumlahPesan))
        return;
    void* kelasPesan = kelasElemenField(fieldItemPesan);
    if (!kelasPesan || !cariField(kelasPesan, "c", fieldObjekPesan))
        return;
    if (!cariField(kelasField(fieldObjekPesan), "content__Text__Standard__Text__Label",
                   fieldLabelPesan))
        return;
    cariField(kelasField(fieldLabelPesan), "mMaxLineCount", fieldMaksBarisLabel);
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

    void* kelasTamu = kelasElemenField(fieldItemTamu);
    if (!cariField(kelasTamu, "key", fieldKunciTamu) ||
        !cariField(kelasTamu, "account", fieldAkunTamu))
        return false;
    if (!cariField(kelasField(fieldAkunTamu), "user_Name", fieldNamaAkun))
        return false;
    if (!cariField(kelasField(fieldMultiManajer), "transfer", fieldTransferMulti))
        return false;
    void* kelasTransfer = kelasField(fieldTransferMulti);
    if (!cariField(kelasTransfer, "pipe", fieldPipeTransfer))
        return false;

    // Callback frame manajer tamu menggerakkan antrean /kickall, jadi pointer
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

    // Nama yang dicetak game untuk pemain ini, dipakai saat jawaban ditambahkan
    // dengan id pengirim "other" supaya tata letak kirinya terjaga.
    if (cariField(kelasDari("Net__Multi__Chat"), "_text", fieldTeksChat))
        cariField(kelasField(fieldTeksChat), "me", fieldTeksSaya);

    // Label template yang mengukur setiap bubble, dipakai untuk menaikkan batas
    // barisnya sebelum jawaban multi-baris pertama. Field yang tidak ada hanya
    // membiarkan batasnya.
    if (cariField(kelasDari("Net__Multi__Chat"), "ui", fieldUiChat) &&
        cariField(kelasDari("UI__Manager"), "measure__Chat__Text__Label", fieldUkurChat))
        cariField(kelasField(fieldUkurChat), "mMaxLineCount", fieldMaksBarisLabel);
    cariFieldLabelPesan();
    return true;
}

} // namespace

void pasangHookRoomKit() {
    siap = cariFieldRoomKit() && target(T_CHAT_SEND).addr && target(T_CHAT_CLEAR).addr &&
           target(T_INPUT_SEND).addr && target(T_PLAYER_KEY).addr && target(T_KICK_SEND).addr &&
           target(T_HOST_KEY).addr && target(T_GUEST_UPDATE).addr;
    if (!siap) {
        LOGE("Field RoomKit tidak ditemukan; perintah chat tidak aktif");
        return;
    }
    LOGI("Field RoomKit siap");
    pasangHook(target(T_CHAT_SEND), reinterpret_cast<void*>(hookKirim));
    pasangHook(target(T_INPUT_SEND), reinterpret_cast<void*>(hookTombolKirim));
    pasangHook(target(T_GUEST_UPDATE), reinterpret_cast<void*>(hookUpdate));
}
