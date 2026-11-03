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
bool idsLogged = false;
// Every eat of a food tile passes through both hooks. The first few calls are
// logged with the tile key so a plate that still vanishes can be traced to the
// id that stopped matching.
int eatLogsLeft = 40;

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

// _Tile.id is filled when the game builds its table, which happens after the
// hooks are installed, so the ids are looked up on first use instead of at
// startup. -1 means "not readable yet", not "missing".
void resolveIds() {
    if ((singleId >= 0 && pluralId >= 0) || !tileClass || !idField.field)
        return;
    static const char* const kSingle[] = {"christmas", "gingerbread_Man", "std"};
    static const char* const kPlural[] = {"christmas", "gingerbread_Man", "plate", "blk"};
    if (singleId < 0)
        singleId = resolveVariant(kSingle, 3, "single");
    if (pluralId < 0)
        pluralId = resolveVariant(kPlural, 4, "plural");
    if (!idsLogged && (singleId >= 0 || pluralId >= 0)) {
        idsLogged = true;
        LOGI("Gingerbread ids: single=%d plural=%d", singleId, pluralId);
    }
}

bool isGingerbread(int32_t id) {
    if (id < 0)
        return false;
    resolveIds();
    if (id == singleId || id == pluralId)
        return true;
    void* entry = tileEntry(id);
    return entry && keyHasGingerbread(readPtr(entry, keyField));
}

void logEat(const char* kind, int32_t id, bool keep) {
    if (eatLogsLeft <= 0)
        return;
    eatLogsLeft--;
    void* entry = tileEntry(id);
    void* key = entry ? readPtr(entry, keyField) : nullptr;
    const uint16_t* chars = stringChars(key);
    int32_t len = stringLength(key);
    char text[52] = {};
    if (chars) {
        int32_t n = len < 48 ? len : 48;
        for (int32_t i = 0; i < n; i++) {
            uint16_t c = chars[i];
            text[i] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
        }
    }
    LOGI("Eat %s: id=%d keep=%d key=%s", kind, id, keep ? 1 : 0, text);
}

// Gingerbread is a Christmas tile the tile converter does not know, so the eat
// path deletes the plate instead of changing it. Claim the id and re-record the
// tile as itself, which keeps the plate on the map for the next bite.
bool termHook(void* self, int32_t id, void* method) {
    bool keep = isGingerbread(id);
    logEat("term", id, keep);
    if (keep)
        return true;
    auto orig = reinterpret_cast<bool (*)(void*, int32_t, void*)>(target(T_EAT_CHANGE_TERM).orig);
    return orig ? orig(self, id, method) : false;
}

int32_t addHook(void* self, int32_t id, void* method) {
    bool keep = isGingerbread(id);
    logEat("add", id, keep);
    if (keep)
        return id;
    auto orig = reinterpret_cast<int32_t (*)(void*, int32_t, void*)>(target(T_EAT_CHANGE_ADD).orig);
    return orig ? orig(self, id, method) : id;
}

// A food leaves the hand only from its own ...__Eat__End, which asks
// Player__Manager.Hh__Change for the empty hand id. Every food and posture has
// its own copy of that method, so each keeps the hand change back and the piece
// stays in hand for the next bite. Ice cream stays out on purpose: its treats end
// the item by design.
template <TargetId id> void eatEndHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(id).orig);
    if (!orig)
        return;
    setKeepHand(true);
    orig(self, method);
    setKeepHand(false);
}

template <TargetId id> void installEatEnd(const char* label) {
    installHook(target(id), reinterpret_cast<void*>(eatEndHook<id>), label);
}

} // namespace

void installFoodHooks() {
    installEatEnd<T_EAT_END_KNEEL>("Player__State__Sp__Tile__Kneel.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SIT_F>("Player__State__Sp__Tile__Sit__F.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SIT_S>("Player__State__Sp__Tile__Sit__S.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_FOOD>("Player__State__Std__Action__Hh__Food.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_KNEEL>("Player__State__Sp__Tile__Kneel.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_SIT_F>("Player__State__Sp__Tile__Sit__F.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_SIT_S>("Player__State__Sp__Tile__Sit__S.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_FOOD>("Player__State__Std__Action__Hh__Food.Sushi__Eat__End");
    installEatEnd<T_EAT_END_BUN_KNEEL>("Player__State__Sp__Tile__Kneel.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_F>("Player__State__Sp__Tile__Sit__F.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_S>("Player__State__Sp__Tile__Sit__S.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_FOOD>(
        "Player__State__Std__Action__Hh__Food.Hot_Cross_Bun__Eat__End");

    tileClass = classOf("_Tile");
    if (!tileClass || !resolveField(tileClass, "tiles", tilesField) ||
        !resolveField(fieldElementClass(tilesField), "key", keyField)) {
        LOGE("Tile table not found; tile eat hooks disabled");
        return;
    }
    resolveField(tileClass, "id", idField);

    installHook(target(T_EAT_CHANGE_TERM), reinterpret_cast<void*>(termHook),
                "Converter__Tile__Type.Term__Eat__Tile__Change");
    installHook(target(T_EAT_CHANGE_ADD), reinterpret_cast<void*>(addHook),
                "Converter__Tile__Type.Eat__Tile__Change__Add");
}
