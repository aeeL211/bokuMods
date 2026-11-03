#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// The game ships 9999 slots per storage type. INT32_MAX makes every store
// unlimited, which is what the mod offers instead of buying slots one by one.
constexpr int32_t kSlotCap = INT32_MAX;

// Every storage type is a Global field, maybe one level down, whose class
// carries the data_Slot_Num counter the loader fills on startup.
struct Slot {
    const char* first;
    const char* second;
    Field firstField;
    Field secondField;
    Field countField;
};

Slot slots[] = {
    {"model", nullptr, {}, {}, {}},       {"face", nullptr, {}, {}, {}},
    {"portrait", nullptr, {}, {}, {}},    {"photo", nullptr, {}, {}, {}},
    {"_favorite", nullptr, {}, {}, {}},   {"_melody", nullptr, {}, {}, {}},
    {"world", nullptr, {}, {}, {}},       {"doll", nullptr, {}, {}, {}},
    {"toolbar", "face", {}, {}, {}},      {"toolbar", "hh", {}, {}, {}},
    {"_paint", "painting", {}, {}, {}},   {"_paint", "palette", {}, {}, {}},
    {"_block", "pack", {}, {}, {}},       {"_block", "own", {}, {}, {}},
    {"_block", "list", {}, {}, {}},       {"_jukebox", "library", {}, {}, {}},
    {"_jukebox", "playlist", {}, {}, {}},
};

bool resolveSlot(Slot& slot) {
    void* globalClass = classOf("Global");
    if (!resolveField(globalClass, slot.first, slot.firstField))
        return false;

    void* holderClass = fieldClass(slot.firstField);
    if (slot.second) {
        if (!resolveField(holderClass, slot.second, slot.secondField))
            return false;
        holderClass = fieldClass(slot.secondField);
    }
    return resolveField(holderClass, "data_Slot_Num", slot.countField);
}

// The loader may already have run before this hook was installed, so the value
// it left behind is raised once at startup too.
void raiseSlot(void* base, const Slot& slot) {
    if (!slot.countField.field)
        return;

    void* holder = readPtr(base, slot.firstField);
    if (slot.second)
        holder = readPtr(holder, slot.secondField);
    if (holder && readInt(holder, slot.countField) < kSlotCap)
        writeInt(holder, slot.countField, kSlotCap);
}

// Every storage type loads its owned slot count through this one function.
// Raising the count to the cap opens the whole store without buying the slots
// one by one.
void loadHook(int32_t* count, void* key, int32_t standalone, void* method) {
    auto orig =
        reinterpret_cast<void (*)(int32_t*, void*, int32_t, void*)>(target(T_SLOT_LOAD).orig);
    if (!orig)
        return;

    orig(count, key, standalone, method);
    if (count && *count < kSlotCap)
        *count = kSlotCap;
}

} // namespace

void installSlotHooks() {
    void* base = staticFieldData(classOf("Global"));

    int ready = 0;
    for (Slot& slot : slots) {
        if (!resolveSlot(slot)) {
            if (slot.second)
                LOGE("Slot field not found: %s.%s", slot.first, slot.second);
            else
                LOGE("Slot field not found: %s", slot.first);
            continue;
        }
        raiseSlot(base, slot);
        ready++;
    }

    int count = static_cast<int>(sizeof(slots) / sizeof(slots[0]));
    if (ready == count)
        LOGI("Slot fields OK");
    else
        LOGE("Slot fields: %d of %d found", ready, count);
    LOGI("Slot cap %d", kSlotCap);

    installHook(target(T_SLOT_LOAD), reinterpret_cast<void*>(loadHook));
}
