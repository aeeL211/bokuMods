#include "hooks.h"

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// The version label on the About page is filled from Refresh__Version__Version().
// The suffix is added to the stock text, so the build is marked as modded
// without hiding the version the game reports.
constexpr char kModSuffix[] = " (modded)";

bool hasText(void* str, const char* needle) {
    const uint16_t* chars = stringChars(str);
    int32_t len = stringLength(str);
    size_t n = strlen(needle);
    if (!chars || static_cast<size_t>(len) < n)
        return false;
    for (int32_t i = 0; i + static_cast<int32_t>(n) <= len; i++) {
        size_t j = 0;
        while (j < n && chars[i + j] == static_cast<uint16_t>(needle[j]))
            j++;
        if (j == n)
            return true;
    }
    return false;
}

void* versionHook(void* self, void* method) {
    auto orig = reinterpret_cast<void* (*)(void*, void*)>(target(T_ABOUT_VERSION).orig);
    if (!orig)
        return nullptr;
    void* stock = orig(self, method);
    if (!stock || !stringChars(stock))
        return stock;
    if (hasText(stock, kModSuffix))
        return stock;

    // The version string is short; a longer one means something unexpected is on
    // the other end, so it is left alone instead of being copied blindly.
    constexpr int32_t kMaxLen = 240;
    int32_t len = stringLength(stock);
    const uint16_t* chars = stringChars(stock);
    if (!chars || len <= 0 || len > kMaxLen)
        return stock;

    uint16_t buffer[kMaxLen + sizeof(kModSuffix)];
    int32_t used = 0;
    for (int32_t i = 0; i < len; i++)
        buffer[used++] = chars[i];
    for (size_t i = 0; i + 1 < sizeof(kModSuffix); i++) {
        buffer[used++] = static_cast<uint16_t>(kModSuffix[i]);
    }

    void* marked = newUtf16String(buffer, used);
    if (!marked)
        return stock;
    LOGI("About: version suffix added, len=%d", used);
    return marked;
}

} // namespace

void installAboutHooks() {
    installHook(target(T_ABOUT_VERSION), reinterpret_cast<void*>(versionHook),
                "About.Refresh__Version__Version");
}
