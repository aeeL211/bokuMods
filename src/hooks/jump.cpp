#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

enum Animal { kDog, kPig, kSheep, kHorse, kCow, kDeer, kAlpaca, kAnimalCount };

// Every mount has its own ground input terminator and its own jump start, and
// the field that holds its state object is named after the animal.
struct AnimalSpec {
    const char* field;
    TargetId ground;
    TargetId jump;
};

constexpr AnimalSpec kAnimals[kAnimalCount] = {
    {"dog__L", T_MOUNT_GROUND_DOG, T_MOUNT_JUMP_DOG},
    {"pig", T_MOUNT_GROUND_PIG, T_MOUNT_JUMP_PIG},
    {"sheep", T_MOUNT_GROUND_SHEEP, T_MOUNT_JUMP_SHEEP},
    {"horse", T_MOUNT_GROUND_HORSE, T_MOUNT_JUMP_HORSE},
    {"cow", T_MOUNT_GROUND_COW, T_MOUNT_JUMP_COW},
    {"deer", T_MOUNT_GROUND_DEER, T_MOUNT_JUMP_DEER},
    {"alpaca", T_MOUNT_GROUND_ALPACA, T_MOUNT_JUMP_ALPACA},
};

// Player__Input__Sp__Obj__Animal -> state -> sp -> obj -> animal -> mount state.
Field stateField = {};
Field spField = {};
Field spObjField = {};
Field spAnimalField = {};
Field mountStateField[kAnimalCount] = {};
Field managerField = {};
Field physicsField = {};
Field dirField = {};

// Player__Input__Std__Air
Field airManagerField = {};
Field airInputField = {};
Field airGroundField = {};
Field jumpField = {};
Field startField = {};
Field contactField = {};
Field bottomField = {};

void* mountState(void* self, int animal) {
    void* state = readPtr(self, stateField);
    void* sp = readPtr(state, spField);
    void* obj = readPtr(sp, spObjField);
    void* animals = readPtr(obj, spAnimalField);
    return readPtr(animals, mountStateField[animal]);
}

// The facing direction the stock ground terminator passes to the jump start.
int32_t facingDir(void* manager) { return readInt(readPtr(manager, physicsField), dirField); }

// Holding the interaction button keeps the mount jumping: the stock ground
// terminator only asks the jump key, so answer for it once the stock call had
// nothing else to do.
template <int animal> bool groundHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(kAnimals[animal].ground).orig);
    if (!orig)
        return false;

    void* manager = readPtr(self, managerField);
    if (!manager || !actionHeld(manager))
        return orig(self, method);
    if (orig(self, method))
        return true;

    auto jump =
        reinterpret_cast<void (*)(void*, int32_t, void*)>(target(kAnimals[animal].jump).addr);
    void* state = mountState(self, animal);
    if (!jump || !state)
        return false;
    jump(state, facingDir(manager), nullptr);
    return true;
}

bool jumpPressed(void* self) {
    void* key = readPtr(readPtr(self, airInputField), jumpField);
    return key && readBool(key, startField);
}

// Only while no tile is under the feet, so a normal ground jump stays stock.
bool airborne(void* self) {
    void* phy = readPtr(readPtr(self, airManagerField), physicsField);
    void* contact = readPtr(phy, contactField);
    return contact && !readBool(contact, bottomField);
}

// The air terminator never answers a plain jump, so a mid-air press is replayed
// into the stock ground jump once the stock path found nothing to do.
bool airTermHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_AIR_TERM).orig);
    if (!orig)
        return false;
    if (orig(self, method))
        return true;
    if (!airborne(self) || !jumpPressed(self))
        return false;

    auto jump = reinterpret_cast<bool (*)(void*, void*)>(target(T_GROUND_JUMP).addr);
    void* ground = readPtr(self, airGroundField);
    if (!jump || !ground)
        return false;
    return jump(ground, nullptr);
}

bool resolveMountFields() {
    void* input = classOf("Player__Input__Sp__Obj__Animal");
    if (!resolveField(input, "m", managerField) || !resolveField(input, "state", stateField) ||
        !resolveField(fieldClass(stateField), "sp", spField) ||
        !resolveField(fieldClass(spField), "obj", spObjField) ||
        !resolveField(fieldClass(spObjField), "animal", spAnimalField))
        return false;

    void* animals = fieldClass(spAnimalField);
    for (int i = 0; i < kAnimalCount; i++) {
        if (!resolveField(animals, kAnimals[i].field, mountStateField[i]))
            return false;
    }
    return true;
}

bool resolveJumpFields() {
    if (!resolveField(classOf("Player__Manager"), "phy", physicsField) ||
        !resolveField(fieldClass(physicsField), "dir", dirField) ||
        !resolveField(fieldClass(physicsField), "contact", contactField) ||
        !resolveField(fieldClass(contactField), "bottom", bottomField))
        return false;

    void* air = classOf("Player__Input__Std__Air");
    return resolveField(air, "m", airManagerField) && resolveField(air, "input", airInputField) &&
           resolveField(air, "ground", airGroundField) &&
           resolveField(fieldClass(airInputField), "jump", jumpField) &&
           resolveField(fieldClass(jumpField), "start", startField);
}

template <int animal> void installGroundHook() {
    installHook(target(kAnimals[animal].ground), reinterpret_cast<void*>(groundHook<animal>));
}

void installMountHooks() {
    if (!resolveMountFields()) {
        LOGE("Mount fields not found; mounts stay stock");
        return;
    }
    LOGI("Mount fields OK");
    installGroundHook<kDog>();
    installGroundHook<kPig>();
    installGroundHook<kSheep>();
    installGroundHook<kHorse>();
    installGroundHook<kCow>();
    installGroundHook<kDeer>();
    installGroundHook<kAlpaca>();
}

void installAirJumpHooks() {
    if (!resolveJumpFields()) {
        LOGE("Jump fields not found; mid-air jump stays stock");
        return;
    }
    if (!target(T_GROUND_JUMP).addr) {
        LOGE("Ground jump not found; mid-air jump stays stock");
        return;
    }
    LOGI("Jump fields OK");
    installHook(target(T_AIR_TERM), reinterpret_cast<void*>(airTermHook));
}

} // namespace

void installJumpHooks() {
    installMountHooks();
    installAirJumpHooks();
}
