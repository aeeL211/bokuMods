#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"
#include "toast.h"

namespace {

Field fieldUi = {};
void* kelasIap = nullptr;

void* uiIap() {
    if (!kelasIap || !fieldUi.field)
        return nullptr;
    void* dasar = dataFieldStatis(kelasIap);
    if (!dasar)
        return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(dasar) + fieldUi.offset);
}

// Lewati Play Billing sepenuhnya: serahkan id produk ke jalur penyelesaian milik
// game, yang sama dengan yang akan dipanggil callback toko. UI__Iap._Update lalu
// memberikan candy-nya pada frame berikutnya.
void hookBeli(int32_t productId, void* method) {
    void* ui = uiIap();
    void* selesai = target(T_PURCHASE_COMPLETE).addr;
    if (ui && selesai && target(T_PURCHASE_COMPLETE).method) {
        LOGI("Pembelian gratis: produk=%d", productId);
        auto panggil = reinterpret_cast<void (*)(void*, int32_t, void*)>(selesai);
        panggil(ui, productId, target(T_PURCHASE_COMPLETE).method);
        tampilkanToast("MODS: Pembelian berhasil disimulasikan");
        return;
    }
    LOGE("Pembelian gratis: Iap.ui tidak tersedia, kembali ke toko");
    auto asli = reinterpret_cast<void (*)(int32_t, void*)>(target(T_BUY).orig);
    if (!asli)
        return;
    asli(productId, method);
}

// Gerbang bersama untuk tile, model, dan melodi.
bool hookBukaKunci(void*, int32_t, bool* checked, bool* unlock, void*) {
    if (checked)
        *checked = true;
    if (unlock)
        *unlock = true;
    return true;
}

} // namespace

void pasangHookIap() {
    kelasIap = kelasDari("Iap");
    if (!cariField(kelasIap, "ui", fieldUi)) {
        LOGE("Field Iap.ui tidak ditemukan; pembelian gratis memakai toko");
    }

    pasangHook(target(T_BUY), reinterpret_cast<void*>(hookBeli));
    pasangHook(target(T_UNLOCK_CHECK), reinterpret_cast<void*>(hookBukaKunci));

    if (!target(T_PURCHASE_COMPLETE).addr) {
        LOGE("Target tidak ditemukan: UI__Iap.Purchase__Complete");
    }
}
