#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// The shipped limit is the fallback for a build whose limit object cannot be
// read, so the loader still opens every slot.
constexpr int32_t kFallbackMax = 9999;

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

int32_t slotMax = 0;

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
    if (holder && readInt(holder, slot.countField) < slotMax)
        writeInt(holder, slot.countField, slotMax);
}

int32_t slotCap(void* base) {
    void* globalClass = classOf("Global");
    Field dataField = {};
    Field slotField = {};
    Field maxField = {};
    if (!resolveField(globalClass, "_data", dataField))
        return kFallbackMax;

    void* data = readPtr(base, dataField);
    if (!data || !resolveField(fieldClass(dataField), "slot_Num", slotField))
        return kFallbackMax;

    void* slotNum = readPtr(data, slotField);
    if (!slotNum || !resolveField(fieldClass(slotField), "max", maxField))
        return kFallbackMax;

    int32_t max = readInt(slotNum, maxField);
    return max > 0 ? max : kFallbackMax;
}

// Every storage type loads its owned slot count through this one function.
// Raising the count to the build's top slot number opens the whole store
// without buying the slots one by one.
void loadHook(int32_t* count, void* key, int32_t standalone, void* method) {
    auto orig =
        reinterpret_cast<void (*)(int32_t*, void*, int32_t, void*)>(target(T_SLOT_LOAD).orig);
    if (!orig)
        return;

    orig(count, key, standalone, method);
    if (count && *count < slotMax)
        *count = slotMax;
}

} // namespace

void installSlotHooks() {
    void* base = staticFieldData(classOf("Global"));
    if (base) {
        slotMax = slotCap(base);
    } else {
        slotMax = kFallbackMax;
        LOGE("Global fields not found; using fallback slot cap %d", slotMax);
    }

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
    LOGI("Slot cap %d", slotMax);

    installHook(target(T_SLOT_LOAD), reinterpret_cast<void*>(loadHook),
                "Global.Data__Load__Data_Slot_Num_");
}
