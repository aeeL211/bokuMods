#include "log.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "hook.h"
#include "jni_bridge.h"

#ifdef BOKU_LOG

namespace {

// Setiap sesi menulis berkasnya sendiri di direktori files eksternal aplikasi,
// yaitu Android/data/<package>/files/bokumods-1.log, bokumods-2.log dan
// seterusnya. Path-nya berasal dari Context.getExternalFilesDir() lewat
// UnityPlayer.currentActivity, jadi nama paket tidak pernah ditulis mati dan
// paket yang diganti nama tetap bekerja.
std::atomic<int> fdLog{-1};

// Path berkas yang dibuka sesi ini, disimpan supaya perintah bisa mencetaknya.
char pathSesi[600] = {};

// Activity, dan bersamanya direktori log, baru ada beberapa detik setelah proses
// mulai. Baris dari jendela itu disimpan di sini dan disiram saat berkasnya
// akhirnya terbuka, supaya awal sesi tidak hilang.
constexpr int kBatasTertunda = 32;
constexpr int kPanjangTertunda = 220;
char tertunda[kBatasTertunda][kPanjangTertunda] = {};
std::atomic<int> jumlahTertunda{0};

// Thread pekerja, JNI_OnLoad dan konstruktor semuanya menulis log sebelum
// berkasnya ada, jadi ring tertunda butuh kuncinya sendiri. Penangan sinyal tidak
// pernah mengambilnya: ia menulis langsung ke fdLog, yang sudah terbuka saat
// crash mulai penting.
std::atomic_flag flagTertunda = ATOMIC_FLAG_INIT;

void kunciTertunda() {
    while (flagTertunda.test_and_set(std::memory_order_acquire)) {
    }
}

void bukaKunciTertunda() { flagTertunda.clear(std::memory_order_release); }

void formatWaktu(char* out, size_t size, const char* pola) {
    std::time_t sekarang = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&sekarang, &tm);
    std::strftime(out, size, pola, &tm);
}

// localtime_r dan strftime menghitung zona waktu pada setiap baris, padahal
// beberapa baris biasanya berbagi detik yang sama. Cache thread-local membuat
// hitungan itu hanya sekali per detik, tanpa kunci antar-thread.
const char* waktuSekarang() {
    thread_local std::time_t detik = 0;
    thread_local char teks[16] = {};
    std::time_t sekarang = std::time(nullptr);
    if (sekarang != detik || !teks[0]) {
        std::tm tm = {};
        localtime_r(&sekarang, &tm);
        std::strftime(teks, sizeof(teks), "%H:%M:%S", &tm);
        detik = sekarang;
    }
    return teks;
}

// INFO diberi spasi tambahan sampai lima karakter supaya kolom pesan sejajar
// dengan ERROR.
const char* namaLevel(char tingkat) { return tingkat == 'E' ? "ERROR" : "INFO "; }

const char* namaSignal(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    default:
        return "UNKNOWN";
    }
}

void tulisBaris(const char* baris, int n) {
    // Periksa ulang di bawah kunci: pembuka berkas bisa memasang fdLog di antara
    // pemeriksaan sebelumnya dan penyimpanan ini, dan baris yang tertinggal
    // setelah siraman terakhir tidak akan pernah sampai ke berkas.
    kunciTertunda();
    int fd = fdLog;
    if (fd >= 0) {
        bukaKunciTertunda();
        write(fd, baris, static_cast<size_t>(n));
        return;
    }
    // Buffer penuh membuang baris tertua, bukan memblokir pemanggil.
    int slot = jumlahTertunda.fetch_add(1) % kBatasTertunda;
    if (slot < 0)
        slot = 0;
    snprintf(tertunda[slot], kPanjangTertunda, "%.*s", n, baris);
    bukaKunciTertunda();
}

void siram() {
    kunciTertunda();
    int jumlah = jumlahTertunda.exchange(0);
    if (jumlah > kBatasTertunda)
        jumlah = kBatasTertunda;
    for (int i = 0; i < jumlah; i++) {
        size_t len = strlen(tertunda[i]);
        if (len)
            write(fdLog, tertunda[i], len);
    }
    bukaKunciTertunda();
}

// getExternalFilesDir adalah API publik untuk direktori eksternal per aplikasi;
// memanggilnya menghindari menebak nama paket atau titik mount penyimpanan.
bool direktoriFile(char* out, size_t size) {
    JNIEnv* env = nullptr;
    bool lepas = false;
    if (!lekatJni(&env, &lepas))
        return false;

    bool berhasil = false;
    jobject activity = nullptr;
    jobject folder = nullptr;
    jstring path = nullptr;
    const char* karakter = nullptr;

    if (kelasUnityPlayer()) {
        jfieldID fieldAktivitas =
            env->GetStaticFieldID(kelasUnityPlayer(), "currentActivity", "Landroid/app/Activity;");
        if (fieldAktivitas)
            activity = env->GetStaticObjectField(kelasUnityPlayer(), fieldAktivitas);
    }
    if (activity) {
        jclass kelasActivity = env->GetObjectClass(activity);
        jmethodID getDir = kelasActivity ? env->GetMethodID(kelasActivity, "getExternalFilesDir",
                                                            "(Ljava/lang/String;)Ljava/io/File;")
                                         : nullptr;
        if (getDir)
            folder = env->CallObjectMethod(activity, getDir, nullptr);
        if (kelasActivity)
            env->DeleteLocalRef(kelasActivity);
    }
    if (folder) {
        jclass kelasFile = env->FindClass("java/io/File");
        if (kelasFile) {
            jmethodID getPath =
                env->GetMethodID(kelasFile, "getAbsolutePath", "()Ljava/lang/String;");
            if (getPath)
                path = static_cast<jstring>(env->CallObjectMethod(folder, getPath));
            env->DeleteLocalRef(kelasFile);
        }
    }
    if (path) {
        karakter = env->GetStringUTFChars(path, nullptr);
        if (karakter) {
            snprintf(out, size, "%s", karakter);
            berhasil = true;
        }
    }

    if (karakter)
        env->ReleaseStringUTFChars(path, karakter);
    if (path)
        env->DeleteLocalRef(path);
    if (folder)
        env->DeleteLocalRef(folder);
    if (activity)
        env->DeleteLocalRef(activity);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (lepas)
        cariVmJava()->DetachCurrentThread();
    return berhasil;
}

// Cadangan untuk jendela singkat sebelum UnityPlayer.currentActivity diisi:
// bangun direktori yang sama dari paket proses dan akar penyimpanan. Direktori
// paket sudah ada sejak pemasangan, jadi direktori yang hilang atau tidak bisa
// ditulis berarti tebakannya salah (paket diganti nama, klon, mount lain) dan
// membuatnya akan menyembunyikan log di luar sandbox aplikasi.
bool direktoriLama(char* out, size_t size) {
    char paket[192];
    if (!paketAplikasi(paket, sizeof(paket)))
        return false;

    char akar[512];
    if (!akarPenyimpananEksternal(akar, sizeof(akar)))
        return false;
    char direktori[512];
    snprintf(direktori, sizeof(direktori), "%s/Android/data/%s/files", akar, paket);
    if (access(direktori, W_OK | X_OK) != 0)
        return false;
    snprintf(out, size, "%s", direktori);
    return true;
}

// Berkas sesi membawa nomor sesinya, jadi namanya tetap pendek dan nomor
// tertinggi di folder selalu yang terbaru.
int nomorSesi(const char* nama) {
    int nilai = 0;
    int terpakai = 0;
    if (sscanf(nama, "bokumods-%d.log%n", &nilai, &terpakai) != 1)
        return 0;
    if (terpakai <= 0 || nama[terpakai] != '\0' || nilai < 1)
        return 0;
    return nilai;
}

int sesiTerakhir(const char* pathDirektori) {
    DIR* aliran = opendir(pathDirektori);
    if (!aliran)
        return 0;
    int terakhir = 0;
    while (struct dirent* entri = readdir(aliran)) {
        int nilai = nomorSesi(entri->d_name);
        if (nilai > terakhir)
            terakhir = nilai;
    }
    closedir(aliran);
    return terakhir;
}

// Satu berkas per sesi, bernomor sesuai urutan sesi dimulai. O_EXCL tidak pernah
// menyentuh berkas sebelumnya, dan nomornya hanya bertambah, jadi berkas yang
// dihapus tidak bisa mewariskan nomornya ke sesi berikutnya.
int bukaBaru(const char* pathDirektori, char* path, size_t size, int* galat) {
    int pertama = sesiTerakhir(pathDirektori) + 1;
    for (int i = pertama; i < pertama + 100; i++) {
        snprintf(path, size, "%s/bokumods-%d.log", pathDirektori, i);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd >= 0)
            return fd;
        *galat = errno;
        if (*galat != EEXIST)
            return -1;
    }
    return -1;
}

bool pastikanTerbuka() {
    if (fdLog >= 0)
        return true;
    // Direktori tidak terjangkau sebelum activity ada, jadi coba ulang paling
    // sering setiap dua detik, bukan pada setiap baris.
    static std::atomic<long> percobaanTerakhir{0};
    long sekarang = static_cast<long>(std::time(nullptr));
    long sebelumnya = percobaanTerakhir.load();
    if (sekarang - sebelumnya < 2)
        return false;
    percobaanTerakhir = sekarang;
    bukaFileLog();
    return fdLog >= 0;
}

// Eksepsi IL2CPP yang tidak tertangani mematikan proses, jadi PC dari fault
// terakhir adalah satu-satunya petunjuk andal tentang asal sebuah crash. Penangan
// sebelumnya (Unity memasang satu) dipulihkan sebelum sinyal di-raise ulang,
// supaya tombstone-nya tetap terjadi.
struct SlotSignal {
    int sig;
    struct sigaction lama;
};

// Alamat kembali di stack yang crash menyebut rantai pemanggil, tetapi
// menyelesaikannya dengan dladdr() akan mengambil kunci linker di proses yang
// sudah rusak. Karena itu rentang modul dipotret sekali, sebelum ada crash, dan
// penangannya hanya membandingkan angka. Hanya segmen executable yang disimpan,
// jadi kata data yang kebetulan berada di modul yang dimuat tidak dibaca sebagai
// kode.
struct Modul {
    const char* nama;
    uintptr_t awal;
    uintptr_t akhir;
};

constexpr int kBatasModul = 32;
Modul modul[kBatasModul] = {};
int jumlahModul = 0;

int kumpulkanModul(struct dl_phdr_info* info, size_t, void*) {
    if (jumlahModul >= kBatasModul)
        return 1;
    if (!info->dlpi_name || !info->dlpi_name[0])
        return 0;
    uintptr_t awal = UINTPTR_MAX;
    uintptr_t akhir = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X))
            continue;
        uintptr_t lo = info->dlpi_addr + ph.p_vaddr;
        uintptr_t hi = lo + ph.p_memsz;
        if (lo < awal)
            awal = lo;
        if (hi > akhir)
            akhir = hi;
    }
    if (akhir <= awal)
        return 0;
    const char* garisMiring = strrchr(info->dlpi_name, '/');
    modul[jumlahModul].nama = garisMiring ? garisMiring + 1 : info->dlpi_name;
    modul[jumlahModul].awal = awal;
    modul[jumlahModul].akhir = akhir;
    jumlahModul++;
    return 0;
}

// Pemanggil abort() dan __stack_chk_fail() dicatat oleh probe di bawah, karena PC
// dari sinyalnya sendiri selalu mendarat di dalam libc dan pemindaian frame tidak
// punya frame pointer untuk diikuti. Nilai bukan nol diselesaikan lewat tabel
// modul yang sama, jadi SIGABRT berikutnya menyebut kode yang menyerah.
std::atomic<uintptr_t> pemanggilAbort{0};
std::atomic<uintptr_t> pemanggilStackCheck{0};
void* abortAsli = nullptr;
void* stackCheckAsli = nullptr;

bool tulisFrameKode(int fd, const char* waktu, const char* penanda, uintptr_t alamat) {
    for (int m = 0; m < jumlahModul; m++) {
        if (alamat < modul[m].awal || alamat >= modul[m].akhir)
            continue;
        char baris[512];
        int n = snprintf(baris, sizeof(baris), "[%s] ERROR CRASH %s %s+0x%lx\n", waktu, penanda,
                         modul[m].nama, static_cast<unsigned long>(alamat - modul[m].awal));
        if (n > 0 && fd >= 0)
            write(fd, baris, static_cast<size_t>(n));
        return true;
    }
    return false;
}

using FungsiFatal = void (*)(void);

// Eksepsi yang tidak tertangani, assertion yang gagal dan pemeriksaan heap
// semuanya berakhir di abort(), jadi pemanggilnya menunjukkan komponen yang
// mematikan proses.
void probeAbort() {
    pemanggilAbort.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    auto asli = reinterpret_cast<FungsiFatal>(abortAsli);
    if (asli)
        asli();
    _exit(127);
}

// Kode game sendiri dibangun dengan stack protector, jadi canary yang rusak
// menyebut fungsi yang menulis melewati frame-nya.
void probeStackCheck() {
    pemanggilStackCheck.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    auto asli = reinterpret_cast<FungsiFatal>(stackCheckAsli);
    if (asli)
        asli();
    _exit(127);
}

SlotSignal daftarSlot[] = {
    {SIGSEGV, {}},
    {SIGBUS, {}},
    {SIGILL, {}},
    {SIGABRT, {}},
};

void tanganiCrash(int sig, siginfo_t* info, void* konteks) {
    auto* uc = static_cast<ucontext_t*>(konteks);
    uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
    uintptr_t lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
    uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
    Dl_info dl = {};
    uintptr_t offset = 0;
    const char* nama = "?";
    if (dladdr(reinterpret_cast<void*>(pc), &dl) && dl.dli_fbase) {
        nama = dl.dli_fname;
        offset = pc - reinterpret_cast<uintptr_t>(dl.dli_fbase);
    }
    // x19/x20 adalah pointer akhir/awal dari pemindaian UTF-16 di libunity, jadi
    // pasangan itu menunjukkan apakah panjang string yang salah mendorongnya
    // melewati buffer.
    char waktu[16];
    formatWaktu(waktu, sizeof(waktu), "%H:%M:%S");
    char baris[512];
    int n = snprintf(baris, sizeof(baris),
                     "[%s] ERROR CRASH signal=%s code=%d pc=0x%lx module=%s+0x%lx "
                     "fault=0x%lx lr=0x%lx x19=0x%lx x20=0x%lx\n",
                     waktu, namaSignal(sig), info->si_code, static_cast<unsigned long>(pc), nama,
                     static_cast<unsigned long>(offset), static_cast<unsigned long>(fault),
                     static_cast<unsigned long>(lr),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[19]),
                     static_cast<unsigned long>(uc->uc_mcontext.regs[20]));
    int fd = fdLog;
    if (n > 0 && fd >= 0)
        write(fd, baris, static_cast<size_t>(n));

    // Probe tahu dari mana abort() dipanggil, dan itulah satu-satunya frame yang
    // berarti saat IL2CPP atau runtime C++ memutuskan mengakhiri proses.
    tulisFrameKode(fd, waktu, "abort caller", pemanggilAbort.load(std::memory_order_relaxed));
    tulisFrameKode(fd, waktu, "stack check caller",
                   pemanggilStackCheck.load(std::memory_order_relaxed));

    // Hanya kata yang jatuh di dalam segmen modul executable yang dicetak, jadi
    // nilai heap, data modul dan kata stack basi tetap di luar log.
    uintptr_t sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
    int tampil = 0;
    for (int i = 0; i < 512 && tampil < 16; i++) {
        uintptr_t kandidat = reinterpret_cast<const uintptr_t*>(sp)[i];
        if (tulisFrameKode(fd, waktu, "frame", kandidat))
            tampil++;
    }
    for (SlotSignal& slot : daftarSlot) {
        if (slot.sig == sig) {
            sigaction(sig, &slot.lama, nullptr);
            break;
        }
    }
    raise(sig);
}

} // namespace

const char* pathFileLog() { return pathSesi[0] ? pathSesi : nullptr; }

void bukaFileLog() {
    if (fdLog >= 0)
        return;
    char direktori[512];
    bool dariActivity = direktoriFile(direktori, sizeof(direktori));
    if (!dariActivity && !direktoriLama(direktori, sizeof(direktori)))
        return;

    mkdir(direktori, 0777);

    char path[600];
    int galat = 0;
    int fd = bukaBaru(direktori, path, sizeof(path), &galat);
    bool diperbaiki = false;
    // Direktori adalah satu-satunya hal yang mungkin harus diperbaiki proses ini;
    // bila pembuatan tetap gagal, percobaan ulang pastikanTerbuka berjalan dua
    // detik kemudian.
    if (fd < 0 && (galat == EACCES || galat == EPERM)) {
        chmod(direktori, 0777);
        fd = bukaBaru(direktori, path, sizeof(path), &galat);
        diperbaiki = fd >= 0;
    }
    if (fd < 0)
        return;
    fdLog = fd;
    snprintf(pathSesi, sizeof(pathSesi), "%s", path);

    char waktu[16];
    char tanggal[16];
    formatWaktu(waktu, sizeof(waktu), "%H:%M:%S");
    formatWaktu(tanggal, sizeof(tanggal), "%Y-%m-%d");
    char baris[640];
    int n = snprintf(baris, sizeof(baris), "[%s] INFO  Log dimulai (%s)\n", waktu, tanggal);
    if (n > 0)
        write(fd, baris, static_cast<size_t>(n));
    // Direktorinya selalu Android/data/<package>/files, jadi path lengkap hanya
    // mengulang apa yang sudah diketahui pembaca dan membuat barisnya sulit
    // dipindai.
    const char* garisMiring = strrchr(path, '/');
    const char* nama = garisMiring ? garisMiring + 1 : path;
    n = snprintf(baris, sizeof(baris), "[%s] INFO  Berkas log: %s (sesi baru%s)\n", waktu, nama,
                 dariActivity ? "" : ", cadangan paket");
    if (n > 0)
        write(fd, baris, static_cast<size_t>(n));
    if (diperbaiki) {
        n = snprintf(baris, sizeof(baris),
                     "[%s] ERROR Direktori log tidak bisa ditulis (%d %s), mode diperbaiki\n",
                     waktu, galat, strerror(galat));
        if (n > 0)
            write(fd, baris, static_cast<size_t>(n));
    }
    siram();
}

void cetakLog(char tingkat, const char* fmt, ...) {
    // Prefiks dan pesan ditulis ke satu buffer, jadi tidak ada salinan 400 byte
    // per baris sebelum barisnya ditempel.
    char baris[512];
    int awal = snprintf(baris, sizeof(baris), "[%s] %s ", waktuSekarang(), namaLevel(tingkat));
    if (awal <= 0 || awal >= static_cast<int>(sizeof(baris)) - 1)
        return;

    va_list argumen;
    va_start(argumen, fmt);
    int isi = vsnprintf(baris + awal, sizeof(baris) - static_cast<size_t>(awal) - 1, fmt, argumen);
    va_end(argumen);
    if (isi < 0)
        return;

    // vsnprintf melaporkan panjang sebelum pemotongan, jadi posisi newline dibatasi.
    size_t akhir = static_cast<size_t>(awal) + static_cast<size_t>(isi);
    if (akhir > sizeof(baris) - 2)
        akhir = sizeof(baris) - 2;
    baris[akhir++] = '\n';
    pastikanTerbuka();
    tulisBaris(baris, static_cast<int>(akhir));
}

void pasangLogCrash() {
    dl_iterate_phdr(kumpulkanModul, nullptr);
    for (SlotSignal& slot : daftarSlot) {
        struct sigaction aksi = {};
        aksi.sa_sigaction = tanganiCrash;
        aksi.sa_flags = SA_SIGINFO;
        sigemptyset(&aksi.sa_mask);
        sigaction(slot.sig, &aksi, &slot.lama);
    }
}

// Kedua probe memasang hook langsung ke libc, yang tidak disebut target mana pun,
// jadi setiap kegagalan pemasangan hanya menghilangkan satu frame itu dari blok
// crash berikutnya.
void pasangProbeCrash() {
    void* alamatAbort = dlsym(RTLD_DEFAULT, "abort");
    pasangHookProbe(alamatAbort, reinterpret_cast<void*>(probeAbort), &abortAsli, "abort");
    void* alamatStackCheck = dlsym(RTLD_DEFAULT, "__stack_chk_fail");
    pasangHookProbe(alamatStackCheck, reinterpret_cast<void*>(probeStackCheck), &stackCheckAsli,
                    "__stack_chk_fail");
}

#else

// log.h menyediakan stub tanpa operasi; ini menjaga unit terjemahan tetap tidak
// kosong.
static_assert(true, "");

#endif
