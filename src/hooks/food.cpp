#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

void* tileClass = nullptr;
Field tilesField = {};
Field keyField = {};
Field idField = {};
// Resolved twin of the tile key match. Either may stay -1 when the game data
// does not expose that variant; the key check alone still covers the rest.
int32_t singleId = -1;
int32_t pluralId = -1;
bool keepLogged = false;

constexpr char kNeedle[] = "gingerbread";
constexpr int32_t kNeedleLen = sizeof(kNeedle) - 1;

char lowerAscii(uint16_t c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
}

bool keyHasGingerbread(void* key) {
    const uint16_t* chars = stringChars(key);
    int32_t len = stringLength(key);
    if (!chars || len < kNeedleLen)
        return false;
    for (int32_t i = 0; i + kNeedleLen <= len; i++) {
        int32_t j = 0;
        while (j < kNeedleLen && lowerAscii(chars[i + j]) == kNeedle[j])
            j++;
        if (j == kNeedleLen)
            return true;
    }
    return false;
}

// The registry is the _Tile class that carries the static tile table; its
// entries keep the tile key the game data uses.
void* tileEntry(int32_t id) {
    if (!tileClass || !tilesField.field || !keyField.field || id < 0)
        return nullptr;
    void* base = staticFieldData(tileClass);
    if (!base)
        return nullptr;
    void* tiles = readPtr(base, tilesField);
    if (!tiles || id >= arrayLength(tiles))
        return nullptr;
    return *reinterpret_cast<void**>(arrayData(tiles) + id * static_cast<ptrdiff_t>(sizeof(void*)));
}

bool isGingerbread(int32_t id) {
    if (id < 0)
        return false;
    if (id == singleId || id == pluralId)
        return true;
    void* entry = tileEntry(id);
    return entry && keyHasGingerbread(readPtr(entry, keyField));
}

bool walkRefs(void*& obj, Field& f, const char* const* names, int count) {
    for (int i = 0; i < count; i++) {
        if (!resolveField(fieldClass(f), names[i], f))
            return false;
        obj = readPtr(obj, f);
        if (!obj)
            return false;
    }
    return true;
}

int32_t resolveVariant(const char* const* names, int count, const char* leaf) {
    if (!tileClass || !idField.field)
        return -1;
    void* base = staticFieldData(tileClass);
    if (!base)
        return -1;
    void* obj = readPtr(base, idField);
    if (!obj)
        return -1;
    Field f = idField;
    if (!walkRefs(obj, f, names, count))
        return -1;
    if (!resolveField(fieldClass(f), leaf, f))
        return -1;
    return readInt(obj, f);
}

// Gingerbread is a Christmas tile the tile converter does not know, so the eat
// path deletes the plate instead of changing it. Claim the id and re-record the
// tile as itself, which keeps the plate on the map for the next bite.
bool termHook(void* self, int32_t id, void* method) {
    if (isGingerbread(id)) {
        if (!keepLogged) {
            keepLogged = true;
            LOGI("Gingerbread kept: tile %d", id);
        }
        return true;
    }
    auto orig = reinterpret_cast<bool (*)(void*, int32_t, void*)>(target(T_EAT_CHANGE_TERM).orig);
    return orig ? orig(self, id, method) : false;
}

int32_t addHook(void* self, int32_t id, void* method) {
    if (isGingerbread(id))
        return id;
    auto orig = reinterpret_cast<int32_t (*)(void*, int32_t, void*)>(target(T_EAT_CHANGE_ADD).orig);
    return orig ? orig(self, id, method) : id;
}

} // namespace

void installFoodHooks() {
    tileClass = classOf("_Tile");
    if (!tileClass || !resolveField(tileClass, "tiles", tilesField) ||
        !resolveField(fieldElementClass(tilesField), "key", keyField)) {
        LOGE("Tile table not found; unlimited gingerbread disabled");
        return;
    }
    if (resolveField(tileClass, "id", idField)) {
        static const char* const kSingle[] = {"christmas", "gingerbread_Man", "std"};
        static const char* const kPlural[] = {"christmas", "gingerbread_Man", "plate", "blk"};
        singleId = resolveVariant(kSingle, 3, "single");
        pluralId = resolveVariant(kPlural, 4, "plural");
        LOGI("Gingerbread ids: single=%d plural=%d", singleId, pluralId);
    }

    installHook(target(T_EAT_CHANGE_TERM), reinterpret_cast<void*>(termHook),
                "Converter__Tile__Type.Term__Eat__Tile__Change");
    installHook(target(T_EAT_CHANGE_ADD), reinterpret_cast<void*>(addHook),
                "Converter__Tile__Type.Eat__Tile__Change__Add");
}
