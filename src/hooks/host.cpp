#include "hooks.h"

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// A position is two floats and travels in the vector registers, so the hook
// keeps the game's own calling convention instead of reading it as an integer.
struct Vector2 {
    float x;
    float y;
};

// Every event names its sender through Player__Manager.Key(), so answering with
// the host key for one call makes that call look like the host on the receiver.
thread_local void* forgedKey = nullptr;

Field pipeMulti;
bool pipeReady = false;

// PhotonNetwork.InRoom is the game's own answer to "are we inside a room", so
// the host powers are only borrowed while a room exists and the lobby keeps the
// vanilla answers. That keeps the host panel, and its close-room button, out of
// the menus, where the close request would look like a closed room.
bool inRoom() {
    auto fn = reinterpret_cast<bool (*)(void*)>(target(T_PHOTON_IN_ROOM).addr);
    return fn && fn(target(T_PHOTON_IN_ROOM).method);
}

// The host panel, the player permission list and the room settings all return
// early unless this is true, so forcing it opens the host controls in a room
// owned by someone else. Those panel sends carry no sender check on the
// receiver, so every client applies them once the panel can call them.
bool hostStatusHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_HOST_STATUS).orig);
    return (orig && orig(self, method)) || inRoom();
}

// The room's save-world action and the creator-only room actions ask this
// instead of the host, so both answers have to agree or those controls stay
// disabled in the panel.
bool creatorHook(void* self, void* method) {
    auto orig = reinterpret_cast<bool (*)(void*, void*)>(target(T_HOST_CREATOR).orig);
    return (orig && orig(self, method)) || inRoom();
}

void* hostKey(void* multi) {
    auto key = reinterpret_cast<void* (*)(void*, void*)>(target(T_HOST_KEY).addr);
    if (!key || !multi)
        return nullptr;
    return key(multi, target(T_HOST_KEY).method);
}

// The kick receive compares this answer against the host key on the host, so
// the forged value is only returned on the thread that is inside sendAsHost().
void* keyHook(void* self, void* method) {
    auto orig = reinterpret_cast<void* (*)(void*, void*)>(target(T_PLAYER_KEY).orig);
    if (forgedKey)
        return forgedKey;
    return orig ? orig(self, method) : nullptr;
}

// A host-only request is accepted only when its sender is the host key, so one
// send is made with the local key swapped for the host key. The sender stays
// untouched for every other event, which keeps the local player's own identity.
void sendAsHost(void* pipe, void* arg, void* method, void (*orig)(void*, void*, void*)) {
    void* key = pipeReady ? hostKey(readPtr(pipe, pipeMulti)) : nullptr;
    if (!key) {
        orig(pipe, arg, method);
        return;
    }
    void* saved = forgedKey;
    forgedKey = key;
    orig(pipe, arg, method);
    forgedKey = saved;
}

// A kick is accepted only when the sender slot names the host, so the local
// kick button is sent with the host key in front of it.
void kickHook(void* self, void* key, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_KICK_SEND).orig);
    if (!orig)
        return;
    LOGI("Kick sent with host key");
    sendAsHost(self, key, method, orig);
}

// The host refuses an explosion request from a player without destroy rights,
// so the request is sent as if the host asked for it.
void explosionHook(void* self, void* payload, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_EXPLOSION_SEND).orig);
    if (!orig)
        return;
    sendAsHost(self, payload, method, orig);
}

// The host applies a tile request without any permission check when Enforce is
// set, which is the game's own path for a forced edit.
void addRequestHook(void* self, Vector2 pos, int id, void* str, int font, bool eff, bool,
                    bool callback, void* method) {
    auto orig =
        reinterpret_cast<void (*)(void*, Vector2, int, void*, int, bool, bool, bool, void*)>(
            target(T_TILE_ADD_REQUEST).orig);
    if (!orig)
        return;
    orig(self, pos, id, str, font, eff, true, callback, method);
}

void removeRequestHook(void* self, Vector2 pos, int id, void* str, int effType, bool, bool destroy,
                       bool callback, void* method) {
    auto orig =
        reinterpret_cast<void (*)(void*, Vector2, int, void*, int, bool, bool, bool, void*)>(
            target(T_TILE_REMOVE_REQUEST).orig);
    if (!orig)
        return;
    orig(self, pos, id, str, effType, true, destroy, callback, method);
}

void replaceRequestHook(void* self, Vector2 removePos, int removeId, void* removeStr,
                        int removeFont, Vector2 addPos, int addId, void* addStr, int addFont,
                        bool eff, bool, bool callback, void* method) {
    auto orig =
        reinterpret_cast<void (*)(void*, Vector2, int, void*, int, Vector2, int, void*, int, bool,
                                  bool, bool, void*)>(target(T_TILE_REPLACE_REQUEST).orig);
    if (!orig)
        return;
    orig(self, removePos, removeId, removeStr, removeFont, addPos, addId, addStr, addFont, eff,
         true, callback, method);
}

} // namespace

void installHostHooks() {
    bool keyReady =
        installHook(target(T_PLAYER_KEY), reinterpret_cast<void*>(keyHook), "Player__Manager.Key");
    pipeReady = keyReady && resolveField(classOf("Net__Multi__Transfer__Pipe"), "multi", pipeMulti);
    LOGI("Host sender field %s", pipeReady ? "OK" : "missing, kick and explosion stay vanilla");

    installHook(target(T_HOST_STATUS), reinterpret_cast<void*>(hostStatusHook),
                "Multi.Term__Status__Host");
    installHook(target(T_HOST_CREATOR), reinterpret_cast<void*>(creatorHook),
                "Multi.Term__Creator__Self");
    installHook(target(T_TILE_ADD_REQUEST), reinterpret_cast<void*>(addRequestHook),
                "Transfer__Client.Tile__Add__Request");
    installHook(target(T_TILE_REMOVE_REQUEST), reinterpret_cast<void*>(removeRequestHook),
                "Transfer__Client.Tile__Remove__Request");
    installHook(target(T_TILE_REPLACE_REQUEST), reinterpret_cast<void*>(replaceRequestHook),
                "Transfer__Client.Tile__Replace__Request");
    installHook(target(T_KICK_SEND), reinterpret_cast<void*>(kickHook),
                "Pipe.Host__Client__Connect__Kick__Send");
    installHook(target(T_EXPLOSION_SEND), reinterpret_cast<void*>(explosionHook),
                "Pipe.Client__Explosion__Range__Request__Send");
}
