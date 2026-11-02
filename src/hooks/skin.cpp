#include "hooks.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// A model string is a few hundred separated fields. The cap only stops a
// corrupt length from walking past the string.
constexpr int32_t kMaxModelChars = 16384;
constexpr size_t kMaxModelBytes = static_cast<size_t>(kMaxModelChars) * 3 + 1;
// One entry per distinct skin a session can show; a repeat costs one compare.
constexpr int kMaxHashes = 512;
constexpr int kMaxSlot = 1000;
constexpr size_t kMaxPathBytes = 1024;

char modelText[kMaxModelBytes];
char pathText[kMaxPathBytes];
char fileText[kMaxModelBytes];

uint64_t modelHashes[kMaxHashes];
int modelHashCount = 0;
int nextSlot = 1;
bool slotReady = false;

Field guestA = {};
Field managerData = {};
Field dataManagerModel = {};
Field modelFilename = {};
bool fieldsReady = false;
bool notReadyLogged = false;

uint64_t hashText(const char* text, size_t len) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < len; i++) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool seenHash(uint64_t hash) {
    for (int i = 0; i < modelHashCount; i++) {
        if (modelHashes[i] == hash)
            return true;
    }
    return false;
}

void rememberHash(uint64_t hash) {
    if (modelHashCount < kMaxHashes)
        modelHashes[modelHashCount++] = hash;
}

// The model folder belongs to the same Manager that holds the guest list, so
// the game's own writer lands the file in Save/Model without any path guess.
// Its file names live in a holder that a half-built store does not have yet,
// and the path builder dereferences it, so that store is reported as absent.
void* dataModel(void* guestManager) {
    if (!fieldsReady)
        return nullptr;
    void* manager = readPtr(guestManager, guestA);
    void* data = readPtr(manager, managerData);
    void* model = readPtr(data, dataManagerModel);
    return readPtr(model, modelFilename) ? model : nullptr;
}

// The game's own path for a model slot, so the layout is never guessed here.
bool slotPath(void* model, int slot) {
    auto fn = reinterpret_cast<void* (*)(void*, int32_t, void*)>(target(T_DATA_MODEL_PATH).addr);
    return fn &&
           toUtf8(fn(model, slot, target(T_DATA_MODEL_PATH).method), pathText, sizeof(pathText));
}

bool slotExists(void* model, int slot) {
    return slotPath(model, slot) && access(pathText, F_OK) == 0;
}

void writeSlot(void* model, int slot, void* text) {
    auto fn =
        reinterpret_cast<void (*)(void*, int32_t, void*, void*)>(target(T_DATA_MODEL_WRITE).addr);
    if (fn)
        fn(model, slot, text, target(T_DATA_MODEL_WRITE).method);
}

// A skin from an earlier session already owns its slot, so the hash of every
// Data.txt on disk is loaded once and the next write continues after them.
bool readSlot(void* model, int slot) {
    if (!slotPath(model, slot))
        return false;
    int fd = open(pathText, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, fileText, sizeof(fileText) - 1);
    close(fd);
    if (n > 0)
        rememberHash(hashText(fileText, static_cast<size_t>(n)));
    return true;
}

void seedSlots(void* model) {
    int slot = 1;
    while (slot <= kMaxSlot && readSlot(model, slot))
        slot++;
    nextSlot = slot;
    LOGI("Skin slots: %d saved, next %d", modelHashCount, nextSlot);
}

// Every guest arrives with the string the game itself stores in Data.txt, so
// one copy per distinct skin goes into the first free model slot.
void saveModel(void* guestManager, void* modelStr) {
    if (!fieldsReady || !modelStr)
        return;
    void* model = dataModel(guestManager);
    if (!model) {
        if (fieldsReady && !notReadyLogged) {
            notReadyLogged = true;
            LOGE("Skin model not ready, skins from this point are skipped");
        }
        return;
    }
    if (!toUtf8(modelStr, modelText, sizeof(modelText)))
        return;

    if (!slotReady) {
        slotReady = true;
        seedSlots(model);
    }
    uint64_t hash = hashText(modelText, strlen(modelText));
    if (seenHash(hash))
        return;
    if (nextSlot > kMaxSlot) {
        LOGE("Skin slots full, %d skins kept", modelHashCount);
        return;
    }

    writeSlot(model, nextSlot, modelStr);
    if (!slotExists(model, nextSlot)) {
        LOGE("Skin write failed: slot %d", nextSlot);
        return;
    }
    rememberHash(hash);
    LOGI("Skin saved: slot %d (%zu bytes)", nextSlot, strlen(modelText));
    nextSlot++;
}

using AddFn = void (*)(void*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void*,
                       void*, void*, void*, void*, bool, bool, bool, bool, bool, bool, void*);

// The guest list is built from this one call on both host and client, and its
// Model argument is the full skin string, so nothing has to be parsed here.
void addHook(void* self, void* user, int32_t z, int32_t x, int32_t y, int32_t dir, int32_t s,
             int32_t obj, void* key, void* dollName, void* parent, void* model, void* ping,
             bool build, bool detonate, bool saveWorld, bool dollInteract, bool dollMove,
             bool dollSetting, void* method) {
    saveModel(self, model);

    auto orig = reinterpret_cast<AddFn>(target(T_GUEST_ADD).orig);
    if (orig)
        orig(self, user, z, x, y, dir, s, obj, key, dollName, parent, model, ping, build, detonate,
             saveWorld, dollInteract, dollMove, dollSetting, method);
}

} // namespace

void installSkinHooks() {
    void* guestClass = classOf("Guest__Manager");
    fieldsReady = resolveField(guestClass, "a", guestA);
    void* managerClass = fieldsReady ? fieldClass(guestA) : nullptr;
    fieldsReady = fieldsReady && resolveField(managerClass, "data", managerData);
    void* dataClass = fieldsReady ? fieldClass(managerData) : nullptr;
    fieldsReady = fieldsReady && resolveField(dataClass, "model", dataManagerModel);
    void* modelClass = fieldsReady ? fieldClass(dataManagerModel) : nullptr;
    fieldsReady = fieldsReady && resolveField(modelClass, "_filename", modelFilename);
    LOGI("Skin model fields %s", fieldsReady ? "OK" : "missing, skins stay untouched");

    installHook(target(T_GUEST_ADD), reinterpret_cast<void*>(addHook), "Guest__Manager.Add");
}
