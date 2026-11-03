#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field sizeA = {};
Field managerTile = {};
Field tileWorldSize = {};
Field worldSizeMax = {};
Field worldSizeMulti = {};
Field maxW = {};
Field maxH = {};
Field maxPreset = {};
Field presetW = {};
Field presetH = {};
Field multiMax = {};
bool capReady = false;

// The host owns the build and destroy permissions and rejects anything a guest
// tries to do beyond them, so both flags now pass through unchanged. Only the
// save-world flag is still forced, because the host sends it once at the start
// and a guest gets no other chance to enable it.
void permissionStartHook(void* self, bool build, bool destroy, bool, void* method) {
    LOGI("Permission: start game build=%d destroy=%d", build, destroy);
    auto orig =
        reinterpret_cast<void (*)(void*, bool, bool, bool, void*)>(target(T_PERMISSION_START).orig);
    if (!orig)
        return;
    orig(self, build, destroy, true, method);
}

void permissionSaveHook(void* self, bool, void* method) {
    LOGI("Permission: save world received");
    auto orig = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PERMISSION_SAVE).orig);
    if (!orig)
        return;
    orig(self, true, method);
}

// The room list drops every room whose world area is above the limit of this
// device, and the join and info panels reuse the same test. Answering true
// keeps those rooms listed whatever their size.
bool roomSizeHook(void*, int, int, void*) { return true; }

// The size page reads max.w and max.h as the slider limit, but the tile manager
// lowered that cap to this device's tier at boot, long before the hooks exist.
// Restoring the game's own top preset each time the page opens lifts the limit
// without a number of our own, and the room area follows the new limit.
void raiseWorldCap(void* self) {
    if (!capReady)
        return;
    void* manager = readPtr(self, sizeA);
    void* tile = readPtr(manager, managerTile);
    void* world = readPtr(tile, tileWorldSize);
    void* max = readPtr(world, worldSizeMax);
    void* preset = readPtr(max, maxPreset);
    int32_t w = readInt(preset, presetW);
    int32_t h = readInt(preset, presetH);
    if (w <= 0 || h <= 0)
        return;
    int32_t oldW = readInt(max, maxW);
    int32_t oldH = readInt(max, maxH);
    if (oldW >= w && oldH >= h)
        return;
    writeInt(max, maxW, w);
    writeInt(max, maxH, h);
    writeInt(readPtr(world, worldSizeMulti), multiMax, w * h);
    LOGI("World size cap raised from %dx%d to %dx%d", oldW, oldH, w, h);
}

// The page builds its slider and its current size in this call, so the cap is
// raised before the original runs and the first open already sees it.
void capHook(void* self, void* method) {
    raiseWorldCap(self);
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_WORLD_CAP).orig);
    if (orig)
        orig(self, method);
}

bool resolveWorldSize() {
    if (!resolveField(classOf("UI__Menu__World__New__Setting__Size"), "a", sizeA))
        return false;
    void* managerClass = fieldClass(sizeA);
    if (!resolveField(managerClass, "tile", managerTile))
        return false;
    void* tileClass = fieldClass(managerTile);
    if (!resolveField(tileClass, "_world_Size", tileWorldSize))
        return false;
    void* worldClass = fieldClass(tileWorldSize);
    if (!resolveField(worldClass, "max", worldSizeMax) ||
        !resolveField(worldClass, "multi", worldSizeMulti))
        return false;
    void* maxClass = fieldClass(worldSizeMax);
    if (!resolveField(maxClass, "w", maxW) || !resolveField(maxClass, "h", maxH) ||
        !resolveField(maxClass, "_3", maxPreset))
        return false;
    void* presetClass = fieldClass(maxPreset);
    return resolveField(presetClass, "w", presetW) && resolveField(presetClass, "h", presetH) &&
           resolveField(fieldClass(worldSizeMulti), "max", multiMax);
}

} // namespace

void installMultiplayerHooks() {
    installHook(target(T_PERMISSION_START), reinterpret_cast<void*>(permissionStartHook),
                "Client.Permission__Start_Game");
    installHook(target(T_PERMISSION_SAVE), reinterpret_cast<void*>(permissionSaveHook),
                "Client.Permission__Save_World__Receive");
    installHook(target(T_ROOM_SIZE), reinterpret_cast<void*>(roomSizeHook),
                "Room_List.Term__Room__Data__Get__World_Size_");
    capReady = resolveWorldSize();
    LOGI("World size fields %s", capReady ? "OK" : "missing, cap stays per device");
    installHook(target(T_WORLD_CAP), reinterpret_cast<void*>(capHook), "Size._Start");
}
