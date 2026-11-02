#include "hooks.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "il2cpp_api.h"
#include "jni_bridge.h"
#include "log.h"
#include "targets.h"

namespace {

// Portraits are small: a name and four integer lists. The cap only protects the
// hook from a corrupt length, not a real limit the game enforces.
constexpr int32_t kMaxPortraitChars = 4096;
constexpr size_t kMaxPortraitBytes = kMaxPortraitChars * 4;
constexpr int kMaxSkinFiles = 500;

// One buffer for the converted string and one for the folder scan, so the hook
// does not put 16 KB on the game thread stack on every portrait refresh.
char skinText[kMaxPortraitBytes];
char skinOther[kMaxPortraitBytes];

// A skin already on disk keeps its number across sessions, so the folder is
// read once and the hashes of its files are kept instead of re-reading them.
uint64_t skinHashes[kMaxSkinFiles];
int skinCount = 0;
int skinNext = 0;
bool skinCacheReady = false;

size_t putCodePoint(uint32_t code, char* out) {
    if (code < 0x80) {
        out[0] = static_cast<char>(code);
        return 1;
    }
    if (code < 0x800) {
        out[0] = static_cast<char>(0xC0 | (code >> 6));
        out[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (code >> 12));
        out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (code >> 18));
    out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (code & 0x3F));
    return 4;
}

bool portraitText(void* str, char* out, size_t size) {
    int32_t len = stringLength(str);
    const uint16_t* chars = stringChars(str);
    if (len <= 0 || len > kMaxPortraitChars || !chars)
        return false;

    size_t used = 0;
    for (int32_t i = 0; i < len; i++) {
        uint32_t code = chars[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < len) {
            uint32_t low = chars[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (code >= 0xD800 && code <= 0xDFFF)
            code = 0xFFFD;
        if (used + 4 >= size)
            return false;
        used += putCodePoint(code, out + used);
    }
    out[used] = '\0';
    return used > 0;
}

// The skin folder sits next to the log, in the game's own external files dir.
// That dir belongs to this process, so no storage permission is involved.
bool skinDir(char* out, size_t size) {
    char root[512];
    if (!externalStorageRoot(root, sizeof(root)))
        return false;
    char package[192];
    if (!appPackage(package, sizeof(package)))
        return false;
    snprintf(out, size, "%s/Android/data/%s/files/Skin", root, package);
    return true;
}

uint64_t textHash(const char* text, size_t len) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < len; i++) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool knownSkin(uint64_t hash) {
    for (int i = 0; i < skinCount; i++) {
        if (skinHashes[i] == hash)
            return true;
    }
    return false;
}

// The first number with no file is the next slot to fill, so a gap left by a
// deleted skin is reused instead of skipped.
int freeSlot(const char* dir, int from) {
    char path[1024];
    for (int i = from; i <= kMaxSkinFiles; i++) {
        snprintf(path, sizeof(path), "%s/skin_%d.txt", dir, i);
        if (access(path, F_OK) != 0)
            return i;
    }
    return 0;
}

void loadSkinDir(const char* dir) {
    char path[1024];
    for (int i = 1; i <= kMaxSkinFiles; i++) {
        snprintf(path, sizeof(path), "%s/skin_%d.txt", dir, i);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        ssize_t n = read(fd, skinOther, sizeof(skinOther) - 1);
        close(fd);
        if (n > 0 && skinCount < kMaxSkinFiles)
            skinHashes[skinCount++] = textHash(skinOther, static_cast<size_t>(n));
    }
    skinNext = freeSlot(dir, 1);
}

void writeSkin(const char* dir, uint64_t hash) {
    if (!skinNext) {
        LOGE("Skin folder full: %s", dir);
        return;
    }

    char path[1024];
    size_t len = strlen(skinText);
    snprintf(path, sizeof(path), "%s/skin_%d.txt", dir, skinNext);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) {
        LOGE("Skin write failed: %s (%d %s)", path, errno, strerror(errno));
        skinNext = freeSlot(dir, skinNext + 1);
        return;
    }

    ssize_t n = write(fd, skinText, len);
    close(fd);
    if (n != static_cast<ssize_t>(len)) {
        LOGE("Skin write incomplete: %s", path);
        return;
    }
    if (skinCount < kMaxSkinFiles)
        skinHashes[skinCount++] = hash;
    LOGI("Skin saved: skin_%d.txt (%zu bytes)", skinNext, len);
    skinNext = freeSlot(dir, skinNext + 1);
}

// A skin already on disk keeps its number, so a second look at the same player
// writes nothing. A new one takes the first free number.
void savePortrait(void* str) {
    if (!portraitText(str, skinText, sizeof(skinText)))
        return;
    uint64_t hash = textHash(skinText, strlen(skinText));

    char dir[768];
    if (!skinDir(dir, sizeof(dir)))
        return;
    mkdir(dir, 0777);
    if (!skinCacheReady) {
        skinCacheReady = true;
        loadSkinDir(dir);
    }
    if (knownSkin(hash))
        return;
    writeSkin(dir, hash);
}

// Every portrait the game renders is parsed here, whether it came from the
// account database or from a guest in the room, so this is the one place that
// sees every skin the player looks at. The parser returns a managed object, so
// the hook hands back the same pointer instead of copying its fields.
void* parseHook(void* d, void* method) {
    savePortrait(d);

    auto orig = reinterpret_cast<void* (*)(void*, void*)>(target(T_PORTRAIT_PARSE).orig);
    return orig ? orig(d, method) : nullptr;
}

} // namespace

void installSkinHooks() {
    installHook(target(T_PORTRAIT_PARSE), reinterpret_cast<void*>(parseHook),
                "_Portrait.Str__To__Portrait");
}
