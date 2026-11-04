#include "toast.h"

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "jni_bridge.h"
#include "log.h"

namespace {

// Toast hanya satu baris pendek; nama yang lebih panjang dipotong di batas
// karakter dan ditandai elipsis supaya pesannya tetap terbaca.
constexpr size_t kBatasNama = 64;
constexpr char kElipsis[] = "...";

// Toast.show() butuh Looper di thread pemanggil: tampilan toast digerakkan oleh
// pesan yang dikirim ke Looper itu, jadi thread tanpa Looper tidak bisa
// menampilkannya. Callback hook berjalan di thread game, yang punya Looper
// utama, jadi mereka menampilkan toast langsung; thread pekerja mod tidak punya,
// jadi ia mendapat thread berumur pendek yang menyiapkan Looper-nya sendiri dan
// menjaganya tetap berjalan.
void tampilkanDiLayar(JNIEnv* env, const char* teks) {
    jobject activity = nullptr;
    if (kelasUnityPlayer()) {
        jfieldID fieldAktivitas =
            env->GetStaticFieldID(kelasUnityPlayer(), "currentActivity", "Landroid/app/Activity;");
        if (fieldAktivitas)
            activity = env->GetStaticObjectField(kelasUnityPlayer(), fieldAktivitas);
    }

    jclass kelasToast = env->FindClass("android/widget/Toast");
    jmethodID make =
        kelasToast ? env->GetStaticMethodID(kelasToast, "makeText",
                                            "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
                                            "Landroid/widget/Toast;")
                   : nullptr;
    jstring pesan = env->NewStringUTF(teks);
    jobject objekToast = (make && pesan && activity)
                             ? env->CallStaticObjectMethod(kelasToast, make, activity, pesan, 0)
                             : nullptr;
    if (objekToast) {
        jmethodID show = env->GetMethodID(kelasToast, "show", "()V");
        if (show)
            env->CallVoidMethod(objekToast, show);
        env->DeleteLocalRef(objekToast);
    }
    if (pesan)
        env->DeleteLocalRef(pesan);
    if (activity)
        env->DeleteLocalRef(activity);
    if (kelasToast)
        env->DeleteLocalRef(kelasToast);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("Toast gagal: %s", teks);
    }
}

bool adaLooper(JNIEnv* env) {
    jclass kelasLooper = env->FindClass("android/os/Looper");
    jmethodID metodeLooper =
        kelasLooper ? env->GetStaticMethodID(kelasLooper, "myLooper", "()Landroid/os/Looper;")
                    : nullptr;
    jobject looper =
        metodeLooper ? env->CallStaticObjectMethod(kelasLooper, metodeLooper) : nullptr;
    if (looper)
        env->DeleteLocalRef(looper);
    if (kelasLooper)
        env->DeleteLocalRef(kelasLooper);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return looper != nullptr;
}

void* threadToast(void* argumen) {
    char teks[384];
    snprintf(teks, sizeof(teks), "%s", static_cast<const char*>(argumen));
    free(argumen);

    JNIEnv* env = nullptr;
    bool lepas = false;
    if (!lekatJni(&env, &lepas))
        return nullptr;

    jclass kelasLooper = env->FindClass("android/os/Looper");
    jmethodID prepare =
        kelasLooper ? env->GetStaticMethodID(kelasLooper, "prepare", "()V") : nullptr;
    if (prepare)
        env->CallStaticVoidMethod(kelasLooper, prepare);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    tampilkanDiLayar(env, teks);

    // Menjaga pesan milik toast tetap mengalir di versi Android lama, tempat
    // tampilannya ditampilkan dan disembunyikan lewat penangan di thread
    // pembuatnya.
    jmethodID loop = kelasLooper ? env->GetStaticMethodID(kelasLooper, "loop", "()V") : nullptr;
    if (loop)
        env->CallStaticVoidMethod(kelasLooper, loop);
    return nullptr;
}

// Berkas nama bisa berasal dari editor mana pun, jadi isinya dibaca sebagai byte
// dulu dan tidak pernah ditolak karena encoding-nya. UTF-8 yang valid dipakai apa
// adanya, byte order mark UTF-16 mengalihkan ke dekode UTF-16, dan byte lain
// dibaca sebagai Latin-1 supaya berkas lama tetap menampilkan karakter yang
// terbaca, bukan toast kosong.
bool karakterSpasi(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Mengembalikan panjang urutan UTF-8 di s, atau 0 bila tidak valid.
size_t utf8Length(const char* s, size_t tersedia) {
    unsigned char awal = static_cast<unsigned char>(s[0]);
    if (awal < 0x80)
        return 1;
    size_t len = 0;
    uint32_t kode = 0;
    if ((awal & 0xE0) == 0xC0) {
        len = 2;
        kode = awal & 0x1F;
    } else if ((awal & 0xF0) == 0xE0) {
        len = 3;
        kode = awal & 0x0F;
    } else if ((awal & 0xF8) == 0xF0) {
        len = 4;
        kode = awal & 0x07;
    } else {
        return 0;
    }
    if (len > tersedia)
        return 0;
    for (size_t i = 1; i < len; i++) {
        unsigned char berikut = static_cast<unsigned char>(s[i]);
        if ((berikut & 0xC0) != 0x80)
            return 0;
        kode = (kode << 6) | (berikut & 0x3F);
    }
    if ((len == 2 && kode < 0x80) || (len == 3 && kode < 0x800) || (len == 4 && kode < 0x10000)) {
        return 0;
    }
    if (kode > 0x10FFFF || (kode >= 0xD800 && kode <= 0xDFFF))
        return 0;
    return len;
}

uint32_t bacaTitikKode(const char* s, size_t len) {
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    if (len == 1)
        return p[0];
    if (len == 2)
        return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if (len == 3)
        return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
}

size_t kodekanUtf8(uint32_t kode, char* out) {
    if (kode < 0x80) {
        out[0] = static_cast<char>(kode);
        return 1;
    }
    if (kode < 0x800) {
        out[0] = static_cast<char>(0xC0 | (kode >> 6));
        out[1] = static_cast<char>(0x80 | (kode & 0x3F));
        return 2;
    }
    if (kode < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (kode >> 12));
        out[1] = static_cast<char>(0x80 | ((kode >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (kode & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (kode >> 18));
    out[1] = static_cast<char>(0x80 | ((kode >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((kode >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (kode & 0x3F));
    return 4;
}

struct BufferNama {
    char* out;
    size_t size;
    size_t terpakai;
    size_t jumlahKarakter;
};

// Karakter kontrol tidak membawa nama yang terlihat, jadi dilewati, bukan
// menggagalkan pembacaan. Mengembalikan false begitu namanya cukup panjang untuk
// dipotong.
bool tambahKarakter(BufferNama& buf, uint32_t kode) {
    if (kode < 0x20 || kode == 0x7F)
        return true;
    if (buf.jumlahKarakter == kBatasNama || buf.terpakai + 4 + sizeof(kElipsis) > buf.size)
        return false;
    buf.terpakai += kodekanUtf8(kode, buf.out + buf.terpakai);
    buf.jumlahKarakter++;
    return true;
}

// Mengembalikan true bila nama dipotong sebelum unit UTF-16 terakhir.
bool bacaUtf16(const unsigned char* s, size_t len, bool littleEndian, BufferNama& buf) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint32_t unit = littleEndian ? (s[i] | (s[i + 1] << 8)) : ((s[i] << 8) | s[i + 1]);
        uint32_t kode = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            uint32_t rendah = 0;
            if (i + 3 < len) {
                const unsigned char* berikut = s + i + 2;
                rendah = littleEndian ? (berikut[0] | (berikut[1] << 8))
                                      : ((berikut[0] << 8) | berikut[1]);
            }
            if (rendah >= 0xDC00 && rendah <= 0xDFFF) {
                kode = 0x10000 + ((unit - 0xD800) << 10) + (rendah - 0xDC00);
                i += 2;
            } else {
                kode = 0xFFFD;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            kode = 0xFFFD;
        }
        if (!tambahKarakter(buf, kode))
            return true;
    }
    return false;
}

// Mengembalikan true bila nama dipotong sebelum karakter terakhir. UTF-8 yang
// tidak valid dibaca byte demi byte sebagai Latin-1, yang menjaga nama
// Windows-1252 tetap terbaca.
bool bacaByte(const char* s, size_t len, BufferNama& buf) {
    for (size_t i = 0; i < len;) {
        size_t urutan = utf8Length(s + i, len - i);
        uint32_t kode = urutan ? bacaTitikKode(s + i, urutan) : static_cast<unsigned char>(s[i]);
        i += urutan ? urutan : 1;
        if (!tambahKarakter(buf, kode))
            return true;
    }
    return false;
}

// Berkas nama seharusnya berada di samping game yang terpasang, tetapi build
// lebih lama menyimpannya di direktori <package>.mods, yang juga menjadi
// direktori game sendiri saat klon memakai nama itu. Keduanya dicoba, jadi kedua
// tata letak berkas tetap terbaca.
bool pathNamaPengguna(char* out, size_t size, bool pakaiDirMods) {
    char akar[512];
    if (!akarPenyimpananEksternal(akar, sizeof(akar)))
        return false;
    char paket[192];
    if (!paketAplikasi(paket, sizeof(paket)))
        return false;
    snprintf(out, size, "%s/Android/data/%s%s/files/Setting/User_Name.txt", akar, paket,
             pakaiDirMods ? ".mods" : "");
    return true;
}

// Dibaca saat toast ditampilkan, bukan disimpan di cache: berkasnya kecil, toast
// hanya tampil sekali per sesi, dan nama yang diubah terpakai pada peluncuran
// berikutnya.
bool bacaFileNama(const char* path, char* out, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int galat = errno;
        LOGE("Berkas nama pengguna gagal dibuka: %s (%d %s)", path, galat, strerror(galat));
        return false;
    }
    char mentah[512];
    ssize_t n = read(fd, mentah, sizeof(mentah) - 1);
    int galat = errno;
    close(fd);
    if (n < 0) {
        LOGE("Berkas nama pengguna gagal dibaca: %s (%d %s)", path, galat, strerror(galat));
        return false;
    }
    if (n == 0) {
        LOGE("Berkas nama pengguna kosong: %s", path);
        return false;
    }

    size_t awal = 0;
    size_t akhir = static_cast<size_t>(n);
    while (awal < akhir && karakterSpasi(static_cast<unsigned char>(mentah[awal])))
        awal++;
    while (akhir > awal && karakterSpasi(static_cast<unsigned char>(mentah[akhir - 1])))
        akhir--;
    if (awal == akhir) {
        LOGE("Berkas nama pengguna tidak berisi nama: %s", path);
        return false;
    }

    BufferNama buf = {out, size, 0, 0};
    const auto* byte = reinterpret_cast<const unsigned char*>(mentah + awal);
    size_t len = akhir - awal;
    bool dipotong = false;
    if (len >= 2 && byte[0] == 0xFF && byte[1] == 0xFE)
        dipotong = bacaUtf16(byte + 2, len - 2, true, buf);
    else if (len >= 2 && byte[0] == 0xFE && byte[1] == 0xFF)
        dipotong = bacaUtf16(byte + 2, len - 2, false, buf);
    else
        dipotong = bacaByte(mentah + awal, len, buf);

    if (dipotong) {
        size_t jumlahTitik = sizeof(kElipsis) - 1;
        memcpy(buf.out + buf.terpakai, kElipsis, jumlahTitik);
        buf.terpakai += jumlahTitik;
    }
    buf.out[buf.terpakai] = '\0';
    LOGI("Nama pengguna dimuat: %s", buf.out);
    return true;
}

bool bacaNama(char* out, size_t size) {
    char path[900];
    if (!pathNamaPengguna(path, sizeof(path), false)) {
        LOGE("Lokasi berkas nama pengguna tidak tersedia");
        return false;
    }
    if (bacaFileNama(path, out, size))
        return true;
    if (!pathNamaPengguna(path, sizeof(path), true))
        return false;
    return bacaFileNama(path, out, size);
}

} // namespace

void tampilkanToast(const char* teks) {
    JNIEnv* env = nullptr;
    bool lepas = false;
    if (!lekatJni(&env, &lepas))
        return;

    if (adaLooper(env)) {
        tampilkanDiLayar(env, teks);
    } else {
        char* salinan = strdup(teks);
        if (salinan) {
            pthread_t t;
            if (pthread_create(&t, nullptr, threadToast, salinan) == 0) {
                pthread_detach(t);
            } else {
                free(salinan);
            }
        }
    }
    if (lepas)
        cariVmJava()->DetachCurrentThread();
}

// Toast aktivasi menyebut akun dari berkas manajer mod; berkas yang rusak atau
// hilang jatuh ke pesan masuk biasa, bukan gagal.
void toastMasuk() {
    char nama[320];
    if (!bacaNama(nama, sizeof(nama))) {
        tampilkanToast("MODS: Masuk");
        return;
    }
    char teks[384];
    snprintf(teks, sizeof(teks), "MODS: Masuk sebagai %s", nama);
    tampilkanToast(teks);
}
