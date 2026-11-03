#include "hooks.h"

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Every version label the player can open is filled from one of these methods:
// the About page, the title screen and the multiplayer help page. The suffix is
// added to the stock text, so the build is marked as modded without hiding the
// version the game reports.
constexpr char kModSuffix[] = " (modded)";

// A version string is short; a longer one means something unexpected is on the
// other end, so it is left alone instead of being copied blindly.
constexpr int32_t kMaxVersionLen = 240;

using VersionHook = void* (*)(void*, void*);

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

// Returns the stock text with kModSuffix appended, or the stock text itself when
// it already carries the suffix, is empty, or is too long to be a version label.
void* markVersion(void* stock, const char* label) {
    if (!stock || !stringChars(stock))
        return stock;
    if (hasText(stock, kModSuffix))
        return stock;

    int32_t len = stringLength(stock);
    const uint16_t* chars = stringChars(stock);
    if (!chars || len <= 0 || len > kMaxVersionLen)
        return stock;

    uint16_t buffer[kMaxVersionLen + sizeof(kModSuffix)];
    int32_t used = 0;
    for (int32_t i = 0; i < len; i++)
        buffer[used++] = chars[i];
    for (size_t i = 0; i + 1 < sizeof(kModSuffix); i++)
        buffer[used++] = static_cast<uint16_t>(kModSuffix[i]);

    void* marked = newUtf16String(buffer, used);
    if (!marked)
        return stock;
    LOGI("%s: version suffix added, len=%d", label, used);
    return marked;
}

VersionHook origOf(TargetId id) { return reinterpret_cast<VersionHook>(target(id).orig); }

void* aboutHook(void* self, void* method) {
    VersionHook orig = origOf(T_ABOUT_VERSION);
    return orig ? markVersion(orig(self, method), "About") : nullptr;
}

void* titleHook(void* self, void* method) {
    VersionHook orig = origOf(T_TITLE_VERSION);
    return orig ? markVersion(orig(self, method), "Title") : nullptr;
}

void* helpHook(void* self, void* method) {
    VersionHook orig = origOf(T_HELP_VERSION);
    return orig ? markVersion(orig(self, method), "Help") : nullptr;
}

} // namespace

void installAboutHooks() {
    installHook(target(T_ABOUT_VERSION), reinterpret_cast<void*>(aboutHook));
    installHook(target(T_TITLE_VERSION), reinterpret_cast<void*>(titleHook));
    installHook(target(T_HELP_VERSION), reinterpret_cast<void*>(helpHook));
}
