#include "hooks.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"
#include "toast.h"

namespace {

using ChatSendFn = bool (*)(void*, void*, bool, void*);
using SendInputFn = void (*)(void*, void*);
using KeyFn = void* (*)(void*, void*);
using KickFn = void (*)(void*, void*, void*);
using EmptySendFn = void (*)(void*, void*);
using ClearFn = void (*)(void*, void*);
using ExplosionFn = void (*)(void*, int32_t, float, float, int32_t, void*);
using ShakeSendFn = void (*)(void*, float, float, void*);
using RoomChatFn = void (*)(void*, void*, void*, void*);

constexpr int kMaxPlayers = 16;
constexpr int kMaxTokens = 8;
constexpr size_t kLineSize = 240;
constexpr size_t kNameSize = 48;
constexpr size_t kKeySize = 16;
constexpr int32_t kExplosionStep = 1;

Field chatManager = {};
Field chatInput = {};
Field inputText = {};
Field managerCamera = {};
Field managerGuests = {};
Field managerMulti = {};
Field managerPlayer = {};
Field guestsItems = {};
Field guestsSize = {};
Field guestKey = {};
Field guestZone = {};
Field guestPhy = {};
Field guestAccount = {};
Field accountName = {};
Field playerPhy = {};
Field multiTransfer = {};
Field transferPipe = {};
Field transferHost = {};
Field physicsPos = {};
Field posWorld = {};
bool ready = false;

// A host-only packet passes the receiver because the payload carries the key of
// the sender, and the receiver only compares that string. The override lives on
// the sending thread and is cleared as soon as the packet is gone.
thread_local bool spoofOn = false;
thread_local void* spoofKey = nullptr;

struct Slot {
    void* guest;
};

struct Session {
    void* chat;
    void* multi;
    void* player;
    void* key;
    void* sender;
    bool alone;
};

void readPoint(void* obj, const Field& f, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (!obj || !f.field)
        return;
    const char* base = reinterpret_cast<const char*>(obj) + f.offset;
    x = *reinterpret_cast<const float*>(base);
    y = *reinterpret_cast<const float*>(base + sizeof(float));
}

bool sameText(void* a, void* b) {
    if (!a || !b)
        return false;
    int32_t len = stringLength(a);
    if (len != stringLength(b))
        return false;
    const uint16_t* aChars = stringChars(a);
    const uint16_t* bChars = stringChars(b);
    if (!aChars || !bChars)
        return false;
    for (int32_t i = 0; i < len; i++) {
        if (aChars[i] != bChars[i])
            return false;
    }
    return true;
}

void* utf16Of(const char* text) {
    uint16_t buf[kLineSize];
    size_t n = 0;
    for (; text[n] && n < kLineSize; n++)
        buf[n] = static_cast<uint16_t>(static_cast<unsigned char>(text[n]));
    return newUtf16String(buf, static_cast<int32_t>(n));
}

void stringOf(void* text, char* out, size_t size, const char* fallback) {
    out[0] = '\0';
    if (text && toUtf8(text, out, size) && out[0])
        return;
    snprintf(out, size, "%s", fallback);
}

void* accountNameOf(void* guest) { return readPtr(readPtr(guest, guestAccount), accountName); }

void* ownKey(void* player) {
    auto key = reinterpret_cast<KeyFn>(target(T_PLAYER_KEY).orig);
    if (!player || !key)
        return nullptr;
    return key(player, target(T_PLAYER_KEY).method);
}

void* hostKeyOf(void* multi) {
    auto key = reinterpret_cast<KeyFn>(target(T_HOST_KEY).addr);
    if (!multi || !key)
        return nullptr;
    return key(multi, target(T_HOST_KEY).method);
}

void startSpoof(void* multi) {
    spoofKey = hostKeyOf(multi);
    spoofOn = spoofKey != nullptr;
}

void stopSpoof() {
    spoofOn = false;
    spoofKey = nullptr;
}

void* pipeOf(void* multi) { return readPtr(readPtr(multi, multiTransfer), transferPipe); }

void* hostOf(void* multi) { return readPtr(readPtr(multi, multiTransfer), transferHost); }

// The guest list holds the remote players, so the local player is always slot
// one and the numbers never depend on the room list order the game happens to
// use. A guest carrying our own key is skipped, in case the game also lists the
// local player there.
int collectPlayers(void* chat, void* mine, Slot* out) {
    void* manager = readPtr(chat, chatManager);
    void* list = readPtr(manager, managerGuests);
    void* items = readPtr(list, guestsItems);
    int n = 0;
    out[n++].guest = nullptr;
    if (!items)
        return n;

    int32_t count = readInt(list, guestsSize);
    int32_t capacity = arrayLength(items);
    if (count > capacity)
        count = capacity;

    char* data = arrayData(items);
    for (int32_t i = 0; i < count && n < kMaxPlayers; i++) {
        void* guest = *reinterpret_cast<void**>(data + i * sizeof(void*));
        if (!guest)
            continue;
        if (sameText(readPtr(guest, guestKey), mine))
            continue;
        out[n++].guest = guest;
    }
    return n;
}

// The room chat only draws a sender it can find in the guest list, so the local
// key is used when it is listed and the first guest key otherwise.
void* displayKey(void* chat, void* mine) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(chat, nullptr, slots);
    void* first = nullptr;
    for (int i = 1; i < n; i++) {
        if (!slots[i].guest)
            continue;
        void* key = readPtr(slots[i].guest, guestKey);
        if (!first)
            first = key;
        if (sameText(key, mine))
            return key;
    }
    return first ? first : mine;
}

// The room chat labels the sender itself, so the body is the whole line. A room
// with no other player has no guest row the line can belong to and the game
// drops it, so the answer goes to a toast there instead.
void reply(Session& s, const char* text) {
    if (s.alone) {
        toast(text);
        return;
    }
    auto roomChat = reinterpret_cast<RoomChatFn>(target(T_CHAT_MSG_RECEIVE).addr);
    void* body = utf16Of(text);
    if (!roomChat || !s.sender || !body)
        return;
    roomChat(s.chat, s.sender, body, target(T_CHAT_MSG_RECEIVE).method);
}

void* fieldText(void* chat) { return readPtr(readPtr(chat, chatInput), inputText); }

bool openSession(void* chat, Session& s) {
    void* manager = readPtr(chat, chatManager);
    s.chat = chat;
    s.player = readPtr(manager, managerPlayer);
    s.multi = readPtr(manager, managerMulti);
    if (!s.player || !s.multi)
        return false;
    s.key = ownKey(s.player);
    s.sender = displayKey(chat, s.key);
    Slot slots[kMaxPlayers];
    s.alone = collectPlayers(chat, s.key, slots) <= 1;
    return true;
}

void kickSlot(Session& s, const Slot& slot) {
    if (!slot.guest)
        return;
    void* key = readPtr(slot.guest, guestKey);
    void* pipe = pipeOf(s.multi);
    auto kick = reinterpret_cast<KickFn>(target(T_KICK_SEND).addr);
    if (!key || !pipe || !kick)
        return;
    startSpoof(s.multi);
    kick(pipe, key, target(T_KICK_SEND).method);
    stopSpoof();
}

void explodeAt(Session& s, int32_t zone, float x, float y) {
    void* host = hostOf(s.multi);
    auto send = reinterpret_cast<ExplosionFn>(target(T_EXPLOSION_PLAYER_SEND).addr);
    if (!host || !send)
        return;
    startSpoof(s.multi);
    send(host, zone, x, y, kExplosionStep, target(T_EXPLOSION_PLAYER_SEND).method);
    stopSpoof();
}

int split(char* text, char** out, int max) {
    int n = 0;
    char* p = text;
    while (*p && n < max) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        out[n++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    return n;
}

using Handler = void (*)(Session&, int, char**);

struct Command {
    const char* name;
    int minArgs;
    int maxArgs;
    int page;
    Handler run;
    const char* usage;
};

void listCommand(Session&, int, char**);
void kickCommand(Session&, int, char**);
void kickAllCommand(Session&, int, char**);
void closeCommand(Session&, int, char**);
void shakeCommand(Session&, int, char**);
void explodeCommand(Session&, int, char**);
void boomCommand(Session&, int, char**);
void menuCommand(Session&, int, char**);
void helpCommand(Session&, int, char**);
void statusCommand(Session&, int, char**);
void logCommand(Session&, int, char**);

const Command commands[] = {
    {"list", 0, 0, 3, listCommand, "/list - show players with their number"},
    {"kick", 1, 1, 1, kickCommand, "/kick <n> - kick player number n"},
    {"kickall", 0, 0, 1, kickAllCommand, "/kickall - kick every other player"},
    {"close", 0, 0, 1, closeCommand, "/close - close the room"},
    {"shake", 0, 0, 2, shakeCommand, "/shake - shake every camera"},
    {"explode", 1, 1, 2, explodeCommand, "/explode <n> - explode at player number n"},
    {"boom", 2, 3, 2, boomCommand, "/boom <x> <y> [zone] - explode at a point"},
    {"menu", 0, 1, 3, menuCommand, "/menu [1-3] - show the command menu"},
    {"help", 1, 1, 3, helpCommand, "/help <command> - explain one command"},
    {"status", 0, 0, 3, statusCommand, "/status - show connection details"},
    {"log", 0, 0, 3, logCommand, "/log - show the session log path"},
};

constexpr int kCommandCount = sizeof(commands) / sizeof(commands[0]);

int32_t firstZone(Session& s) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    for (int i = 1; i < n; i++) {
        if (slots[i].guest)
            return readInt(slots[i].guest, guestZone);
    }
    return 0;
}

void listCommand(Session& s, int, char**) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    void* host = hostKeyOf(s.multi);
    char name[kNameSize];
    char line[kLineSize];
    for (int i = 0; i < n; i++) {
        if (!slots[i].guest) {
            snprintf(line, sizeof(line), "#%d you", i + 1);
        } else {
            stringOf(accountNameOf(slots[i].guest), name, sizeof(name), "?");
            bool isHost = sameText(readPtr(slots[i].guest, guestKey), host);
            snprintf(line, sizeof(line), "#%d %s%s", i + 1, name, isHost ? " (host)" : "");
        }
        reply(s, line);
    }
}

void kickCommand(Session& s, int, char** argv) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    int index = atoi(argv[0]);
    if (index < 1 || index > n) {
        reply(s, "No such player");
        return;
    }
    if (!slots[index - 1].guest) {
        reply(s, "You cannot kick yourself");
        return;
    }
    kickSlot(s, slots[index - 1]);
    reply(s, "Kick sent");
}

void kickAllCommand(Session& s, int, char**) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    int sent = 0;
    char line[64];
    for (int i = 1; i < n; i++) {
        kickSlot(s, slots[i]);
        sent++;
    }
    snprintf(line, sizeof(line), "Kick sent to %d player(s)", sent);
    reply(s, line);
}

void closeCommand(Session& s, int, char**) {
    void* pipe = pipeOf(s.multi);
    auto close = reinterpret_cast<EmptySendFn>(target(T_ROOM_CLOSE_SEND).addr);
    if (!pipe || !close) {
        reply(s, "Room close unavailable");
        return;
    }
    startSpoof(s.multi);
    close(pipe, target(T_ROOM_CLOSE_SEND).method);
    stopSpoof();
    reply(s, "Room closed");
}

void shakeCommand(Session& s, int, char**) {
    void* host = hostOf(s.multi);
    auto send = reinterpret_cast<ShakeSendFn>(target(T_EXPLOSION_SHAKE_SEND).addr);
    if (!host || !send) {
        reply(s, "Camera shake unavailable");
        return;
    }
    float x = 0.0f;
    float y = 0.0f;
    readPoint(readPtr(readPtr(s.player, playerPhy), physicsPos), posWorld, x, y);
    startSpoof(s.multi);
    send(host, x, y, target(T_EXPLOSION_SHAKE_SEND).method);
    stopSpoof();

    // The camera shake hook drops every call, so this device is shaken through
    // the original instead of the packet it just sent.
    auto shake = reinterpret_cast<void (*)(void*, void*)>(target(T_SHAKE).orig);
    void* camera = readPtr(readPtr(s.chat, chatManager), managerCamera);
    if (shake && camera)
        shake(camera, target(T_SHAKE).method);
    reply(s, "Camera shake sent");
}

void explodeCommand(Session& s, int, char** argv) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    int index = atoi(argv[0]);
    if (index < 1 || index > n) {
        reply(s, "No such player");
        return;
    }
    if (!slots[index - 1].guest) {
        reply(s, "Pick a guest, not yourself");
        return;
    }
    void* guest = slots[index - 1].guest;
    float x = 0.0f;
    float y = 0.0f;
    readPoint(readPtr(readPtr(guest, guestPhy), physicsPos), posWorld, x, y);
    explodeAt(s, readInt(guest, guestZone), x, y);
    reply(s, "Explosion sent");
}

void boomCommand(Session& s, int argc, char** argv) {
    float x = strtof(argv[0], nullptr);
    float y = strtof(argv[1], nullptr);
    int32_t zone = argc > 2 ? atoi(argv[2]) : firstZone(s);
    explodeAt(s, zone, x, y);
    reply(s, "Explosion sent");
}

void statusCommand(Session& s, int, char**) {
    void* hostKey = hostKeyOf(s.multi);
    char own[kKeySize];
    char host[kKeySize];
    char line[kLineSize];
    stringOf(s.key, own, sizeof(own), "?");
    stringOf(hostKey, host, sizeof(host), "?");
    snprintf(line, sizeof(line), "You: %s", own);
    reply(s, line);
    snprintf(line, sizeof(line), "Host: %s", host);
    reply(s, line);
    reply(s, sameText(s.key, hostKey) ? "Master: yes" : "Master: no");
    Slot slots[kMaxPlayers];
    snprintf(line, sizeof(line), "Players: %d", collectPlayers(s.chat, s.key, slots));
    reply(s, line);
}

void logCommand(Session& s, int, char**) {
    const char* path = logFilePath();
    reply(s, path ? path : "Log file not open");
}

void menuCommand(Session& s, int argc, char** argv) {
    int page = argc > 0 ? atoi(argv[0]) : 1;
    if (page < 1 || page > 3) {
        reply(s, "Menu pages are 1 to 3");
        return;
    }
    char header[32];
    snprintf(header, sizeof(header), "FULL CONTROL %d/3", page);
    reply(s, header);
    for (int i = 0; i < kCommandCount; i++) {
        if (commands[i].page == page)
            reply(s, commands[i].usage);
    }
}

void helpCommand(Session& s, int, char** argv) {
    for (int i = 0; i < kCommandCount; i++) {
        if (strcmp(commands[i].name, argv[0]))
            continue;
        reply(s, commands[i].usage);
        return;
    }
    reply(s, "No such command");
}

// Only a known command is taken off the wire; anything else starting with a
// slash is ordinary chat and reaches the room unchanged.
bool runCommand(Session& s, int argc, char** argv) {
    for (int i = 0; i < kCommandCount; i++) {
        const Command& c = commands[i];
        if (strcmp(c.name, argv[0]))
            continue;
        LOGI("Command: %s", c.name);
        int count = argc - 1;
        if (count < c.minArgs || count > c.maxArgs) {
            reply(s, c.usage);
            return true;
        }
        c.run(s, count, argv + 1);
        return true;
    }
    return false;
}

// The line is copied without its leading slash, so the command table is matched
// by name alone. Anything that is not plain printable ASCII stays ordinary chat.
bool commandText(void* text, char* out, size_t size) {
    const uint16_t* chars = stringChars(text);
    int32_t len = stringLength(text);
    if (!chars || len < 2 || chars[0] != '/')
        return false;
    size_t n = 0;
    for (int32_t i = 1; i < len && n + 1 < size; i++) {
        uint16_t code = chars[i];
        if (code < 0x20 || code > 0x7E)
            return false;
        out[n++] = static_cast<char>(code >= 'A' && code <= 'Z' ? code + 32 : code);
    }
    out[n] = '\0';
    return true;
}

// A known command is answered locally and the box is cleared with the game's own
// reset, so the typed line never reaches the network.
bool handleCommand(void* chat, void* text) {
    char line[kLineSize];
    if (!ready || !commandText(text, line, sizeof(line)))
        return false;
    Session s;
    if (!openSession(chat, s)) {
        LOGE("Command session missing: no player or room");
        return false;
    }
    char* argv[kMaxTokens];
    int argc = split(line, argv, kMaxTokens);
    if (argc < 1 || !runCommand(s, argc, argv))
        return false;
    auto clear = reinterpret_cast<ClearFn>(target(T_CHAT_CLEAR).addr);
    if (clear)
        clear(chat, target(T_CHAT_CLEAR).method);
    return true;
}

// The validator receives the same string the send uses, so a command is answered
// here and the send is skipped by returning false.
bool sendHook(void* self, void* text, bool voice, void* method) {
    LOGI("Chat send: len=%d voice=%d", stringLength(text), voice ? 1 : 0);
    if (handleCommand(self, text))
        return false;
    auto orig = reinterpret_cast<ChatSendFn>(target(T_CHAT_SEND).orig);
    return orig ? orig(self, text, voice, method) : false;
}

// The send button and the keyboard action both land here with the field already
// filled, so the command is taken before the validator runs at all.
void sendButtonHook(void* self, void* method) {
    void* text = fieldText(self);
    LOGI("Send button: len=%d", stringLength(text));
    if (handleCommand(self, text))
        return;
    auto orig = reinterpret_cast<SendInputFn>(target(T_INPUT_SEND).orig);
    if (orig)
        orig(self, method);
}

void* keyHook(void* self, void* method) {
    if (spoofOn && spoofKey)
        return spoofKey;
    auto orig = reinterpret_cast<KeyFn>(target(T_PLAYER_KEY).orig);
    return orig ? orig(self, method) : nullptr;
}

bool resolveFields() {
    if (!resolveField(classOf("Net__Multi__Chat"), "a", chatManager))
        return false;
    if (!resolveField(classOf("Net__Multi__Chat"), "_input_Field", chatInput) ||
        !resolveField(fieldClass(chatInput), "text", inputText))
        return false;
    void* managerClass = fieldClass(chatManager);
    // The camera is only used for the local half of /shake, so a missing field
    // must not turn the whole command set off.
    resolveField(managerClass, "camera", managerCamera);
    if (!resolveField(managerClass, "guests", managerGuests) ||
        !resolveField(managerClass, "multi", managerMulti) ||
        !resolveField(managerClass, "player", managerPlayer))
        return false;

    void* listClass = fieldClass(managerGuests);
    if (!resolveField(listClass, "_items", guestsItems) ||
        !resolveField(listClass, "_size", guestsSize))
        return false;

    void* guestClass = fieldElementClass(guestsItems);
    if (!resolveField(guestClass, "key", guestKey) ||
        !resolveField(guestClass, "zone", guestZone) ||
        !resolveField(guestClass, "phy", guestPhy) ||
        !resolveField(guestClass, "account", guestAccount))
        return false;
    if (!resolveField(fieldClass(guestAccount), "user_Name", accountName))
        return false;
    if (!resolveField(fieldClass(managerPlayer), "phy", playerPhy))
        return false;

    if (!resolveField(fieldClass(guestPhy), "pos", physicsPos))
        return false;
    if (!resolveField(fieldClass(physicsPos), "world", posWorld))
        return false;

    if (!resolveField(fieldClass(managerMulti), "transfer", multiTransfer))
        return false;
    void* transferClass = fieldClass(multiTransfer);
    return resolveField(transferClass, "pipe", transferPipe) &&
           resolveField(transferClass, "host", transferHost);
}

} // namespace

void installFullControlHooks() {
    ready = resolveFields() && target(T_CHAT_SEND).addr && target(T_CHAT_CLEAR).addr &&
            target(T_INPUT_SEND).addr && target(T_CHAT_MSG_RECEIVE).addr &&
            target(T_PLAYER_KEY).addr && target(T_KICK_SEND).addr &&
            target(T_ROOM_CLOSE_SEND).addr && target(T_EXPLOSION_PLAYER_SEND).addr &&
            target(T_EXPLOSION_SHAKE_SEND).addr && target(T_HOST_KEY).addr;
    if (!ready) {
        LOGE("Full control fields missing; chat commands stay off");
        return;
    }
    LOGI("Full control fields OK");
    installHook(target(T_CHAT_SEND), reinterpret_cast<void*>(sendHook));
    installHook(target(T_INPUT_SEND), reinterpret_cast<void*>(sendButtonHook));
    installHook(target(T_PLAYER_KEY), reinterpret_cast<void*>(keyHook));
}
