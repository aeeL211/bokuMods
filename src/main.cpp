#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "hook.h"
#include "hooks/hooks.h"
#include "il2cpp_api.h"
#include "info.h"
#include "jni_bridge.h"
#include "log.h"
#include "protect.h"
#include "targets.h"
#include "toast.h"

#if !defined(__aarch64__)
#error "Hanya arm64-v8a yang didukung"
#endif

namespace {

void* pekerja(void*) {
    LOGI("Pekerja mulai");
    // JNI_OnLoad berjalan setelah konstruktor kembali, jadi kelas activity bisa
    // belum tersimpan saat thread ini mulai.
    for (int i = 0; i < 10 && !kelasUnityPlayer(); i++)
        usleep(100000);
    catatLingkungan();
    if (!inisialisasiProteksi())
        return nullptr;
    if (!inisialisasiShadowHook())
        return nullptr;

    for (int i = 0; i < 120 && !dlopen("libil2cpp.so", RTLD_LAZY | RTLD_NOLOAD); i++) {
        sleep(1);
    }
    sleep(2);

    if (!ikatApi()) {
        LOGE("API IL2CPP tidak ditemukan");
        return nullptr;
    }
    // Unity memasang penangannya sendiri saat startup, jadi ambil alih hanya
    // setelah itu selesai dan sambungkan kembali ke sana dari penangan crash.
    // Probe dipasang setelah tabel modul yang dipakai untuk menyelesaikannya
    // dipotret.
    pasangLogCrash();
    pasangProbeCrash();
    LOGI("API IL2CPP terikat");

    void* domain = nullptr;
    for (int i = 0; i < 60 && !(domain = apiIl2cpp().domain()); i++) {
        sleep(1);
    }
    if (!domain) {
        LOGE("Domain IL2CPP belum siap");
        return nullptr;
    }

    LOGI("Domain siap, scan dimulai");
    apiIl2cpp().attach(domain);
    int ditemukan = scan();
    LOGI("Scan selesai: %d dari %d metode ditemukan", ditemukan, T_COUNT);

    pasangHookIap();
    pasangHookMultiplayer();
    pasangHookChat();
    pasangHookSensitif();
    pasangHookInputTeks();
    pasangHookProfil();
    pasangHookSandi();
    pasangHookRentangSkin();
    pasangHookTentang();
    pasangHookTangan();
    pasangHookMakanan();
    pasangHookPrivasi();
    pasangHookAkun();
    cariFieldAksi();
    pasangHookSenjata();
    pasangHookEfek();
    pasangHookTerpental();
    pasangHookKotoran();
    pasangHookSkin();
    pasangHookSlot();
    pasangHookSpoof();
    pasangHookRoomKit();
    LOGI("Hook terpasang: %d", jumlahHookTerpasang());
    toastMasuk();
    return nullptr;
}

} // namespace

// Sengaja diekspor: ART mencari JNI_OnLoad di library yang baru dimuatnya, jadi
// simbol tersembunyi akan menggagalkan pemuatan.
extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM* vm, void*) {
    setelVmJava(vm);
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        simpanKelasUnityPlayer(env);
    }
    bukaFileLog();
    return JNI_VERSION_1_6;
}

__attribute__((constructor)) static void saatMuat() {
    bukaFileLog();
    LOGI("Library dimuat");
    pthread_t t;
    if (pthread_create(&t, nullptr, pekerja, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGE("Thread gagal dibuat");
    }
}
