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
// Every eaten tile passes through both hooks. The first few calls are logged with
// the tile key so a food that still vanishes can be traced to its id.
int eatLogsLeft = 40;

constexpr char kIceCream[] = "ice_cream";
constexpr char kPopsicle[] = "popsicle";

char lowerAscii(uint16_t c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
}

bool keyHas(void* key, const char* needle, int32_t len) {
    const uint16_t* chars = stringChars(key);
    int32_t keyLen = stringLength(key);
    if (!chars || keyLen < len)
        return false;
    for (int32_t i = 0; i + len <= keyLen; i++) {
        int32_t j = 0;
        while (j < len && lowerAscii(chars[i + j]) == needle[j])
            j++;
        if (j == len)
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

void* tileKey(int32_t id) {
    void* entry = tileEntry(id);
    return entry ? readPtr(entry, keyField) : nullptr;
}

// Ice cream and the popsicle keep their vanilla one-bite end: their treats are
// meant to run out.
bool isIceCream(int32_t id) {
    void* key = tileKey(id);
    return keyHas(key, kIceCream, sizeof(kIceCream) - 1) ||
           keyHas(key, kPopsicle, sizeof(kPopsicle) - 1);
}

void logEat(const char* kind, int32_t id, bool keep) {
    if (eatLogsLeft <= 0)
        return;
    eatLogsLeft--;
    void* key = tileKey(id);
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

// The eat path asks this hook whether the bitten tile has a follow-up tile, then
// asks the next hook for the id to record. Claiming the id and returning it
// unchanged records the same dish again, so the same food can be eaten as often
// as wanted. Foods the converter does not list, which is every festive dish and
// every dish the game data adds later, otherwise fall through to the delete
// branch and are gone after one bite.
bool termHook(void* self, int32_t id, void* method) {
    bool keep = !isIceCream(id);
    logEat("term", id, keep);
    if (keep)
        return true;
    auto orig = reinterpret_cast<bool (*)(void*, int32_t, void*)>(target(T_EAT_CHANGE_TERM).orig);
    return orig ? orig(self, id, method) : false;
}

int32_t addHook(void* self, int32_t id, void* method) {
    bool keep = !isIceCream(id);
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
    LOGI("Tile table OK");

    installHook(target(T_EAT_CHANGE_TERM), reinterpret_cast<void*>(termHook),
                "Converter__Tile__Type.Term__Eat__Tile__Change");
    installHook(target(T_EAT_CHANGE_ADD), reinterpret_cast<void*>(addHook),
                "Converter__Tile__Type.Eat__Tile__Change__Add");
}
