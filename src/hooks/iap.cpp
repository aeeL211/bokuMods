#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"
#include "toast.h"

namespace {

Field uiRef = {};
void* iapClass = nullptr;

void* iapUi() {
    if (!iapClass || !uiRef.field)
        return nullptr;
    void* base = staticFieldData(iapClass);
    if (!base)
        return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(base) + uiRef.offset);
}

// Skip Play Billing entirely: hand the product id to the game's own completion
// path, which is what the store callback would have called. UI__Iap._Update
// then grants the candy on the next frame.
void buyHook(int32_t productId, void* method) {
    void* ui = iapUi();
    void* complete = target(T_PURCHASE_COMPLETE).addr;
    if (ui && complete && target(T_PURCHASE_COMPLETE).method) {
        LOGI("Free purchase: product=%d", productId);
        auto fn = reinterpret_cast<void (*)(void*, int32_t, void*)>(complete);
        fn(ui, productId, target(T_PURCHASE_COMPLETE).method);
        toast("MODS: Purchase simulated successfully");
        return;
    }
    LOGE("Free purchase: Iap.ui unavailable, falling back to the store");
    auto orig = reinterpret_cast<void (*)(int32_t, void*)>(target(T_BUY).orig);
    if (!orig)
        return;
    orig(productId, method);
}

// Shared gate for tiles, models and melodies.
bool unlockHook(void*, int32_t, bool* checked, bool* unlock, void*) {
    if (checked)
        *checked = true;
    if (unlock)
        *unlock = true;
    return true;
}

} // namespace

void installIapHooks() {
    iapClass = classOf("Iap");
    if (!resolveField(iapClass, "ui", uiRef)) {
        LOGE("Field Iap.ui not found; free purchase will use the store");
    }

    installHook(target(T_BUY), reinterpret_cast<void*>(buyHook), "Iap.BuyProductID");
    installHook(target(T_UNLOCK_CHECK), reinterpret_cast<void*>(unlockHook), "_Unlock.Check");

    if (!target(T_PURCHASE_COMPLETE).addr) {
        LOGE("Target not found: UI__Iap.Purchase__Complete");
    }
}
