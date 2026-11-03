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
// A user id is short; a longer one is not worth a map entry.
constexpr size_t kMaxUserBytes = 64;

char modelText[kMaxModelBytes];
char pathText[kMaxPathBytes];
char fileText[kMaxModelBytes];

uint64_t modelHashes[kMaxHashes];
int modelHashCount = 0;
int nextSlot = 1;
bool slotReady = false;

// A saved slot and the user it came from, so the display name that arrives
// later can be written next to the model it belongs to.
struct UserSlot {
    char user[kMaxUserBytes];
    int slot;
};

UserSlot userSlots[kMaxHashes];
int userSlotCount = 0;

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

// The game writes the display name of its own models the same way, so the slot
// gets the usual Name.txt next to Data.txt.
void writeNameSlot(void* model, int slot, void* text) {
    auto fn =
        reinterpret_cast<void (*)(void*, int32_t, void*, void*)>(target(T_DATA_MODEL_NAME).addr);
    if (fn)
        fn(model, slot, text, target(T_DATA_MODEL_NAME).method);
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
// one copy per distinct skin goes into the first free model slot. The slot is
// returned so the name can be attached once the profile arrives.
int saveModel(void* guestManager, void* modelStr) {
    if (!fieldsReady || !modelStr)
        return 0;
    void* model = dataModel(guestManager);
    if (!model) {
        if (fieldsReady && !notReadyLogged) {
            notReadyLogged = true;
            LOGE("Skin model not ready, skins from this point are skipped");
        }
        return 0;
    }
    if (!toUtf8(modelStr, modelText, sizeof(modelText)))
        return 0;

    if (!slotReady) {
        slotReady = true;
        seedSlots(model);
    }
    uint64_t hash = hashText(modelText, strlen(modelText));
    if (seenHash(hash))
        return 0;
    if (nextSlot > kMaxSlot) {
        LOGE("Skin slots full, %d skins kept", modelHashCount);
        return 0;
    }

    writeSlot(model, nextSlot, modelStr);
    if (!slotExists(model, nextSlot)) {
        LOGE("Skin write failed: slot %d", nextSlot);
        return 0;
    }
    rememberHash(hash);
    LOGI("Skin saved: slot %d (%zu bytes)", nextSlot, strlen(modelText));
    return nextSlot++;
}

void rememberUser(void* user, int slot) {
    char text[kMaxUserBytes];
    if (userSlotCount >= kMaxHashes || !toUtf8(user, text, sizeof(text)))
        return;
    memcpy(userSlots[userSlotCount].user, text, strlen(text) + 1);
    userSlots[userSlotCount].slot = slot;
    userSlotCount++;
}

// The guest carries the profile name only after the join handshake, so the name
// is written here, to every slot that user's models were saved under.
void writeUserName(void* guestManager, void* user, void* name) {
    char text[kMaxUserBytes];
    if (!fieldsReady || !name || stringLength(name) == 0 || !toUtf8(user, text, sizeof(text)))
        return;
    void* model = dataModel(guestManager);
    if (!model)
        return;

    for (int i = 0; i < userSlotCount; i++) {
        if (strcmp(userSlots[i].user, text))
            continue;
        writeNameSlot(model, userSlots[i].slot, name);
        LOGI("Skin name: slot %d", userSlots[i].slot);
    }
}

using AddFn = void (*)(void*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void*,
                       void*, void*, void*, void*, bool, bool, bool, bool, bool, bool, void*);

// The guest list is built from this one call on both host and client, and its
// Model argument is the full skin string, so nothing has to be parsed here.
void addHook(void* self, void* user, int32_t z, int32_t x, int32_t y, int32_t dir, int32_t s,
             int32_t obj, void* key, void* dollName, void* parent, void* model, void* ping,
             bool build, bool detonate, bool saveWorld, bool dollInteract, bool dollMove,
             bool dollSetting, void* method) {
    int slot = saveModel(self, model);
    if (slot)
        rememberUser(user, slot);

    auto orig = reinterpret_cast<AddFn>(target(T_GUEST_ADD).orig);
    if (orig)
        orig(self, user, z, x, y, dir, s, obj, key, dollName, parent, model, ping, build, detonate,
             saveWorld, dollInteract, dollMove, dollSetting, method);
}

using NameFn = void (*)(void*, void*, void*, void*);

// The profile arrives after the join, so the display name is written to the
// slots that guest's models were saved under.
void nameHook(void* self, void* user, void* name, void* method) {
    auto orig = reinterpret_cast<NameFn>(target(T_GUEST_NAME).orig);
    if (orig)
        orig(self, user, name, method);
    writeUserName(self, user, name);
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

    installHook(target(T_GUEST_ADD), reinterpret_cast<void*>(addHook));
    installHook(target(T_GUEST_NAME), reinterpret_cast<void*>(nameHook));
}
