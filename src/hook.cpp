#include "hook.h"

#include <shadowhook.h>

#include "log.h"

namespace {

int jumlahTerpasang = 0;

} // namespace

// Inisialisasi sekali dan sedini mungkin: mode tidak bisa berubah setelah
// diinisialisasi dan mode unique membuat sebuah hook bisa memanggil trampoline-nya
// langsung. debuggable tetap false karena ia menulis ke logcat, yang tidak dipakai
// proyek ini.
bool inisialisasiShadowHook() {
    int kode = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (kode) {
        LOGE("ShadowHook gagal diinisialisasi: %d %s", kode, shadowhook_to_errmsg(kode));
        return false;
    }
    LOGI("ShadowHook siap");
    return true;
}

bool pasangHook(Target& entri, void* pengganti) {
    if (!entri.addr) {
        LOGE("Target tidak ditemukan: %s.%s", entri.cls, entri.name);
        return false;
    }
    // Dua berkas bisa menamai method yang sama, dan ShadowHook hanya memiliki satu
    // hook per alamat pada mode unique, jadi sebuah target menyimpan stub pertamanya
    // dan melaporkan pengulangan sebagai terpasang daripada menimpa trampoline yang
    // sudah dipanggil berkas pertama.
    if (entri.stub)
        return true;
    void* stub = shadowhook_hook_func_addr(entri.addr, pengganti, &entri.orig);
    if (!stub) {
        int kode = shadowhook_get_errno();
        LOGE("Hook gagal: %s.%s (error %d, %s)", entri.cls, entri.name, kode,
             shadowhook_to_errmsg(kode));
        return false;
    }
    entri.stub = stub;
    jumlahTerpasang++;
    return true;
}

int jumlahHookTerpasang() { return jumlahTerpasang; }

bool pasangHookProbe(void* alamat, void* pengganti, void** asli, const char* nama) {
    if (!alamat) {
        LOGE("Probe tidak ditemukan: %s", nama);
        return false;
    }
    void* stub = shadowhook_hook_func_addr(alamat, pengganti, asli);
    if (!stub) {
        int kode = shadowhook_get_errno();
        LOGE("Probe gagal: %s (error %d, %s)", nama, kode, shadowhook_to_errmsg(kode));
        return false;
    }
    return true;
}
