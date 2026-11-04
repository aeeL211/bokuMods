#include "hooks.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

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
using ShakeSendFn = void (*)(void*, float, float, void*);
using ShakeFn = void (*)(void*, int32_t, float, void*);
using AddTextFn = void (*)(void*, int32_t, void*, int32_t, int32_t, bool, void*, void*);
using RemoveSendFn = void (*)(void*, void*, void*);
using LeaveFn = void (*)(void*, int32_t, void*);

constexpr int kMaxPlayers = 16;
constexpr int kMaxTokens = 8;
constexpr int kPageCount = 3;
constexpr size_t kLineSize = 240;
constexpr size_t kNameSize = 96;
constexpr size_t kKeySize = 16;
constexpr double kShakeGap = 0.2;
constexpr float kShakeMax = 90.0f;
constexpr double kKickGap = 0.25;
constexpr double kKickWindow = 8.0;
constexpr size_t kKeyChars = 40;

// The stock explosion shake is the strongest the game configures: eight rounds
// at the full camera offset. The camera hooks drop every stock call, so the
// timed shake feeds these values straight to the original.
constexpr int32_t kShakeRounds = 8;
constexpr float kShakeOffset = 1.0f;

Field chatManager = {};
Field chatInput = {};
Field inputText = {};
Field chatMsg = {};
Field msgSender = {};
Field senderSelf = {};
Field senderOther = {};
Field chatText = {};
Field textMe = {};
Field chatUi = {};
Field chatMeasure = {};
Field labelMaxLines = {};
Field chatMsgs = {};
Field msgItems = {};
Field msgCount = {};
Field msgObj = {};
Field msgLabel = {};
Field managerCamera = {};
Field managerGuests = {};
Field managerMulti = {};
Field managerPlayer = {};
Field guestsItems = {};
Field guestsSize = {};
Field guestKey = {};
Field guestAccount = {};
Field accountName = {};
Field playerPhy = {};
Field multiTransfer = {};
Field transferPipe = {};
Field transferHost = {};
Field multiDisconnect = {};
Field disconnectState = {};
Field disconnectHost = {};
Field physicsPos = {};
Field posWorld = {};
Field guestManagerA = {};
bool ready = false;
bool linesReady = false;

// The timed shake keeps the start instant and the next send instant on a clock
// the game cannot pause, so a slow frame does not stretch the requested time.
struct Shake {
    double next;
    double end;
    bool active;
};

Shake shake = {};

// The kick packets are paced one per tick. The queue holds only the target key
// text taken when the command ran, so nothing in it points at a guest, a chat or
// a player object that the next room or the next frame could free.
struct KickAll {
    bool active;
    double next;
    double end;
    int sent;
    int nextIndex;
    int count;
    char keys[kMaxPlayers][kKeyChars];
};

KickAll kickAll = {};

// A host-only packet passes the receiver because the payload carries the key of
// the sender, and the receiver only compares that string. The override lives on
// the sending thread and is cleared as soon as the packet is gone.
thread_local bool spoofOn = false;
thread_local void* spoofKey = nullptr;

struct Slot {
    void* guest;
};

struct Session {
    void* manager;
    void* chat;
    void* multi;
    void* player;
    void* key;
    int32_t senderId;
    bool canReply;
};

double nowSeconds() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

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

// The answer text is UTF-8, because a player name is converted to UTF-8 before it
// is put in a line, and the chat table stores UTF-16. Copying the bytes one to one
// turned every non-ASCII name into broken symbols, so the text is decoded here.
void utf8Step(const char* text, size_t& i, uint32_t& code) {
    unsigned char lead = static_cast<unsigned char>(text[i]);
    int extra = 0;
    if (lead < 0x80) {
        code = lead;
        i += 1;
        return;
    }
    if ((lead & 0xE0) == 0xC0) {
        code = lead & 0x1F;
        extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        code = lead & 0x0F;
        extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        code = lead & 0x07;
        extra = 3;
    } else {
        i += 1;
        code = 0xFFFD;
        return;
    }
    for (int k = 1; k <= extra; k++) {
        unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) {
            i += 1;
            code = 0xFFFD;
            return;
        }
        code = (code << 6) | (next & 0x3F);
    }
    i += static_cast<size_t>(extra) + 1;
}

void* utf16Of(const char* text) {
    uint16_t buf[kLineSize];
    size_t n = 0;
    for (size_t i = 0; text[i] && n + 2 < kLineSize;) {
        uint32_t code = 0;
        utf8Step(text, i, code);
        if (code >= 0x10000) {
            code -= 0x10000;
            buf[n++] = static_cast<uint16_t>(0xD800 + (code >> 10));
            buf[n++] = static_cast<uint16_t>(0xDC00 + (code & 0x3FF));
        } else {
            buf[n++] = static_cast<uint16_t>(code);
        }
    }
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

// A bubble is a prefab instance, so it starts with the prefab's own line cap.
// The bubble a multi-line answer just added is the last entry of the message
// list, and lifting its cap keeps the text from being clipped to one line. A
// missing list or a bubble that is not built yet leaves the cap alone.
void liftMsgLabel(void* chat) {
    if (!msgLabel.field)
        return;
    void* list = readPtr(chat, chatMsgs);
    void* items = readPtr(list, msgItems);
    int32_t count = readInt(list, msgCount);
    int32_t capacity = arrayLength(items);
    if (!items || count < 1 || count > capacity)
        return;
    void* msg = *reinterpret_cast<void**>(arrayData(items) + (count - 1) * sizeof(void*));
    void* label = readPtr(readPtr(msg, msgObj), msgLabel);
    writeInt(label, labelMaxLines, 0);
}

// The room chat draws a line only when its sender id equals _msg.sender.self or
// _msg.sender.other. A line under the self id is drawn on the right, the side
// the game keeps for the player's own messages, so an answer is added under the
// "other" id instead and takes the left layout of a received message. The name
// the game prints for this player travels with it, because the other layout
// labels the bubble from the name it is given. A missing field keeps the self
// line. The bubble is one NGUI label, and NGUI breaks its text at every newline,
// so a body can hold several lines. Without a message object the answer falls
// back to a toast instead of going silent.
int32_t replySender(Session& s) {
    int32_t other = readInt(readPtr(readPtr(s.chat, chatMsg), msgSender), senderOther);
    return other > 0 && other != s.senderId ? other : s.senderId;
}

void* replyName(Session& s) {
    void* me = readPtr(readPtr(s.chat, chatText), textMe);
    if (me && stringLength(me) > 0)
        return me;
    return s.key ? s.key : utf16Of("");
}

void reply(Session& s, const char* text) {
    auto add = reinterpret_cast<AddTextFn>(target(T_CHAT_MSG_ADD).addr);
    if (!s.canReply || !add) {
        toast(text);
        return;
    }
    void* body = utf16Of(text);
    void* name = replyName(s);
    if (!body || !name) {
        toast(text);
        return;
    }
    add(s.chat, replySender(s), name, -1, 0, false, body, target(T_CHAT_MSG_ADD).method);
    if (strchr(text, '\n'))
        liftMsgLabel(s.chat);
}

// A command answer reads as one bubble while its lines are packed into one
// label, so lines are joined with a newline here. The block is cut before it
// outgrows the UTF-16 text buffer or turns into a bubble taller than the chat
// panel, so a long answer becomes a few short bubbles instead of one tall one.
struct Block {
    char text[kLineSize];
    size_t len;
    int lines;
};

constexpr size_t kBlockChars = 228;
constexpr int kBlockLines = 6;

void blockReset(Block& block) {
    block.text[0] = '\0';
    block.len = 0;
    block.lines = 0;
}

void blockFlush(Session& s, Block& block) {
    if (block.lines)
        reply(s, block.text);
    blockReset(block);
}

void blockAdd(Session& s, Block& block, const char* line) {
    size_t add = strlen(line);
    if (add >= kBlockChars) {
        blockFlush(s, block);
        reply(s, line);
        return;
    }
    size_t gap = block.lines ? 1 : 0;
    if (block.lines >= kBlockLines || block.len + gap + add > kBlockChars)
        blockFlush(s, block);
    if (block.lines)
        block.text[block.len++] = '\n';
    memcpy(block.text + block.len, line, add);
    block.len += add;
    block.text[block.len] = '\0';
    block.lines++;
}

// The bubble height comes from a template label that the game switches to
// ResizeHeight before every measurement, and NGUI stops adding lines at that
// label's maxLineCount. Lifting the cap once lets a multi-line block count every
// line, so the bubble grows with the text instead of clipping it.
void prepareChatLines(void* chat) {
    if (linesReady || !chat)
        return;
    void* label = readPtr(readPtr(chat, chatUi), chatMeasure);
    if (!label)
        return;
    linesReady = true;
    int32_t cap = readInt(label, labelMaxLines);
    if (cap == 0)
        return;
    writeInt(label, labelMaxLines, 0);
    LOGI("Chat line cap lifted: %d", cap);
}

void* fieldText(void* chat) { return readPtr(readPtr(chat, chatInput), inputText); }

bool openSession(void* chat, Session& s) {
    void* manager = readPtr(chat, chatManager);
    s.manager = manager;
    s.chat = chat;
    s.player = readPtr(manager, managerPlayer);
    s.multi = readPtr(manager, managerMulti);
    if (!s.player || !s.multi)
        return false;
    s.key = ownKey(s.player);
    void* msg = readPtr(chat, chatMsg);
    s.canReply = msg && msgSender.field && senderSelf.field && target(T_CHAT_MSG_ADD).addr;
    s.senderId = s.canReply ? readInt(msg, senderSelf) : 0;
    return true;
}

// The stock kick button sends two packets: the kick makes the guest show the
// notice, and the guest remove takes it off every list. Both carry the sender
// key, so the same override makes a client accept them as host packets. A kick
// needs only the target key and the pipe, so a queued key can be replayed later
// without touching the guest object it came from.
bool kickKey(void* multi, void* key) {
    void* pipe = pipeOf(multi);
    auto kick = reinterpret_cast<KickFn>(target(T_KICK_SEND).addr);
    auto remove = reinterpret_cast<RemoveSendFn>(target(T_GUEST_REMOVE_SEND).addr);
    if (!key || !pipe || !kick)
        return false;
    startSpoof(multi);
    kick(pipe, key, target(T_KICK_SEND).method);
    if (remove)
        remove(pipe, key, target(T_GUEST_REMOVE_SEND).method);
    stopSpoof();
    return true;
}

bool guestKeyText(void* guest, char* out) {
    out[0] = '\0';
    stringOf(readPtr(guest, guestKey), out, kKeyChars, "");
    return out[0] != '\0';
}

void finishKickAll() {
    kickAll.active = false;
    LOGI("Kickall done: %d guests", kickAll.sent);
    if (kickAll.sent == 0)
        return;
    char line[kLineSize];
    snprintf(line, sizeof(line), "Kickall: %d tamu ditendang", kickAll.sent);
    toast(line);
}

// The heartbeat runs while a room is open, which is what paces the drain. Only
// the pipe and the manager are read here, and both come from objects the frame is
// using right now, so a queue left over a room change can only fire at strangers.
void kickAllTick(void* self) {
    if (!kickAll.active)
        return;
    double now = nowSeconds();
    if (kickAll.nextIndex >= kickAll.count || now > kickAll.end) {
        finishKickAll();
        return;
    }
    if (now < kickAll.next)
        return;
    void* multi = readPtr(readPtr(self, guestManagerA), managerMulti);
    if (!multi) {
        LOGI("Kickall stopped: room gone");
        kickAll.active = false;
        return;
    }
    kickAll.next = now + kKickGap;
    LOGI("Kickall: %d of %d", kickAll.nextIndex + 1, kickAll.count);
    void* key = utf16Of(kickAll.keys[kickAll.nextIndex]);
    kickAll.nextIndex++;
    if (kickKey(multi, key))
        kickAll.sent++;
    else
        LOGE("Kick failed: guest has no key or pipe");
}

// The camera hook drops both stock entry points, so the local half of a shake
// goes through the original of the two-argument call.
void applyShake(void* camera, int32_t rounds, float offset) {
    auto shakeFn = reinterpret_cast<ShakeFn>(target(T_SHAKE_NUM).orig);
    if (shakeFn && camera)
        shakeFn(camera, rounds, offset, target(T_SHAKE_NUM).method);
}

// One shake round at the game's top strength: the local camera is shaken here,
// and the packet puts the other clients' cameras on the same spot.
void shakeOnce(Session& s) {
    void* camera = readPtr(s.manager, managerCamera);
    applyShake(camera, kShakeRounds, kShakeOffset);
    void* host = hostOf(s.multi);
    auto send = reinterpret_cast<ShakeSendFn>(target(T_EXPLOSION_SHAKE_SEND).addr);
    if (!host || !send)
        return;
    float x = 0.0f;
    float y = 0.0f;
    readPoint(readPtr(readPtr(s.player, playerPhy), physicsPos), posWorld, x, y);
    startSpoof(s.multi);
    send(host, x, y, target(T_EXPLOSION_SHAKE_SEND).method);
    stopSpoof();
}

// The guest manager updates every frame while a room is open, which is the
// heartbeat the timed shake needs; the chat panel can be closed mid-shake.
void shakeTick(void* self) {
    if (!shake.active)
        return;
    double now = nowSeconds();
    void* manager = readPtr(self, guestManagerA);
    void* camera = readPtr(manager, managerCamera);
    if (now >= shake.end) {
        shake.active = false;
        applyShake(camera, 0, 0.0f);
        return;
    }
    if (now < shake.next)
        return;
    shake.next = now + kShakeGap;
    Session s = {};
    s.manager = manager;
    s.multi = readPtr(manager, managerMulti);
    s.player = readPtr(manager, managerPlayer);
    if (!s.multi || !s.player)
        return;
    shakeOnce(s);
}

void updateHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_GUEST_UPDATE).orig);
    if (orig)
        orig(self, method);
    // A game call that unwinds through a send would skip the matching stop, and a
    // leaked override makes every later key lookup answer with the host key.
    stopSpoof();
    shakeTick(self);
    kickAllTick(self);
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
void menuCommand(Session&, int, char**);
void helpCommand(Session&, int, char**);
void statusCommand(Session&, int, char**);
void logCommand(Session&, int, char**);

const Command commands[] = {
    {"list", 0, 0, 3, listCommand, "/list - daftar pemain dan nomornya"},
    {"kick", 1, 1, 1, kickCommand, "/kick <n> - tendang pemain nomor n"},
    {"kickall", 0, 0, 1, kickAllCommand, "/kickall - tendang semua tamu, host tetap"},
    {"close", 0, 0, 1, closeCommand, "/close - tutup room lalu keluar"},
    {"shake", 1, 1, 2, shakeCommand, "/shake <detik> - getarkan kamera paling kuat"},
    {"menu", 0, 1, 3, menuCommand, "/menu [1-3] - daftar perintah"},
    {"help", 0, 1, 3, helpCommand, "/help [perintah] - penjelasan perintah"},
    {"status", 0, 0, 3, statusCommand, "/status - detail koneksi"},
    {"log", 0, 0, 3, logCommand, "/log - lokasi file log sesi ini"},
};

constexpr int kCommandCount = sizeof(commands) / sizeof(commands[0]);

void listCommand(Session& s, int, char**) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    void* host = hostKeyOf(s.multi);
    Block block;
    blockReset(block);
    char name[kNameSize];
    char line[kLineSize];
    for (int i = 0; i < n; i++) {
        if (!slots[i].guest) {
            snprintf(line, sizeof(line), "#%d kamu", i + 1);
        } else {
            stringOf(accountNameOf(slots[i].guest), name, sizeof(name), "?");
            bool isHost = sameText(readPtr(slots[i].guest, guestKey), host);
            snprintf(line, sizeof(line), "#%d %s%s", i + 1, name, isHost ? " (host)" : "");
        }
        blockAdd(s, block, line);
    }
    blockFlush(s, block);
}

void kickCommand(Session& s, int, char** argv) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    int index = atoi(argv[0]);
    if (index < 1 || index > n) {
        reply(s, "Pemain nomor itu tidak ada");
        return;
    }
    if (!slots[index - 1].guest) {
        reply(s, "Tidak bisa menendang diri sendiri");
        return;
    }
    if (sameText(readPtr(slots[index - 1].guest, guestKey), hostKeyOf(s.multi))) {
        reply(s, "Host tidak bisa ditendang");
        return;
    }
    if (!kickKey(s.multi, readPtr(slots[index - 1].guest, guestKey))) {
        reply(s, "Tendangan gagal");
        return;
    }
    reply(s, "Tendangan terkirim");
}

// The host is listed as an ordinary guest, and one frame cannot carry every
// packet, so the command only fills the queue and the guest update sends one kick
// per tick. The queue holds key text, never a guest or chat pointer, so it stays
// valid while the room changes under it.
void kickAllCommand(Session& s, int, char**) {
    Slot slots[kMaxPlayers];
    int n = collectPlayers(s.chat, s.key, slots);
    void* host = hostKeyOf(s.multi);
    int count = 0;
    for (int i = 1; i < n && count < kMaxPlayers; i++) {
        char key[kKeyChars];
        if (!guestKeyText(slots[i].guest, key))
            continue;
        if (sameText(readPtr(slots[i].guest, guestKey), host))
            continue;
        snprintf(kickAll.keys[count], kKeyChars, "%s", key);
        LOGI("Kickall queue %d: %s", count + 1, key);
        count++;
    }
    LOGI("Kickall start: %d guests of %d slots", count, n);
    if (count == 0) {
        reply(s, "Tidak ada tamu untuk ditendang");
        return;
    }
    kickAll.active = true;
    kickAll.next = 0.0;
    kickAll.end = nowSeconds() + kKickWindow;
    kickAll.sent = 0;
    kickAll.nextIndex = 0;
    kickAll.count = count;
    reply(s, "Menendang tamu satu per satu");
}

// The stock host close sends the packet and then starts the same timed leave the
// menu button uses, which is what actually empties the room and removes it from
// the list. Only the host owns the room, so a guest is refused instead of being
// dropped into the host leave flow.
bool canLeaveRoom() {
    return multiDisconnect.field && disconnectState.field && disconnectHost.field &&
           target(T_DISCONNECT_INITIATIVE).addr;
}

void startLeave(void* multi) {
    auto leave = reinterpret_cast<LeaveFn>(target(T_DISCONNECT_INITIATIVE).addr);
    void* state = readPtr(readPtr(multi, multiDisconnect), disconnectState);
    if (!leave || !state) {
        LOGE("Close: leave state missing");
        return;
    }
    leave(multi, readInt(state, disconnectHost), target(T_DISCONNECT_INITIATIVE).method);
}

void closeCommand(Session& s, int, char**) {
    if (!sameText(s.key, hostKeyOf(s.multi))) {
        reply(s, "Hanya host yang bisa menutup room");
        return;
    }
    void* pipe = pipeOf(s.multi);
    auto send = reinterpret_cast<EmptySendFn>(target(T_ROOM_CLOSE_SEND).addr);
    bool canSend = pipe && send;
    if (!canSend && !canLeaveRoom()) {
        reply(s, "Tutup room tidak tersedia");
        return;
    }
    reply(s, canSend ? "Room ditutup" : "Kamu keluar dari room");
    if (canSend) {
        startSpoof(s.multi);
        send(pipe, target(T_ROOM_CLOSE_SEND).method);
        stopSpoof();
    }
    if (canLeaveRoom())
        startLeave(s.multi);
}

// The seconds are capped so a typo cannot hammer every client with packets; the
// timer itself is advanced by the guest update hook, never by a blocking loop.
void shakeCommand(Session& s, int, char** argv) {
    float seconds = strtof(argv[0], nullptr);
    if (!(seconds > 0.0f)) {
        reply(s, "Detik harus lebih dari nol");
        return;
    }
    if (seconds > kShakeMax)
        seconds = kShakeMax;
    shake.active = true;
    shake.next = 0.0;
    shake.end = nowSeconds() + seconds;
    char line[kLineSize];
    snprintf(line, sizeof(line), "Getar kamera paling kuat %.1f detik", seconds);
    reply(s, line);
}

void statusCommand(Session& s, int, char**) {
    void* hostKey = hostKeyOf(s.multi);
    char own[kKeySize];
    char host[kKeySize];
    char line[kLineSize];
    Block block;
    blockReset(block);
    stringOf(s.key, own, sizeof(own), "?");
    stringOf(hostKey, host, sizeof(host), "?");
    snprintf(line, sizeof(line), "Key kamu: %s", own);
    blockAdd(s, block, line);
    snprintf(line, sizeof(line), "Key host: %s", host);
    blockAdd(s, block, line);
    blockAdd(s, block, sameText(s.key, hostKey) ? "Kamu host: ya" : "Kamu host: tidak");
    Slot slots[kMaxPlayers];
    snprintf(line, sizeof(line), "Pemain: %d", collectPlayers(s.chat, s.key, slots));
    blockAdd(s, block, line);
    blockFlush(s, block);
}

void logCommand(Session& s, int, char**) {
    const char* path = logFilePath();
    reply(s, path ? path : "File log belum dibuka");
}

void menuCommand(Session& s, int argc, char** argv) {
    int from = 1;
    int to = kPageCount;
    if (argc > 0) {
        int page = atoi(argv[0]);
        if (page < 1 || page > kPageCount) {
            reply(s, "Halaman menu 1 sampai 3");
            return;
        }
        from = page;
        to = page;
    }
    char header[kLineSize];
    for (int page = from; page <= to; page++) {
        Block block;
        blockReset(block);
        snprintf(header, sizeof(header), "KONTROL PENUH %d/%d", page, kPageCount);
        blockAdd(s, block, header);
        for (int i = 0; i < kCommandCount; i++) {
            if (commands[i].page == page)
                blockAdd(s, block, commands[i].usage);
        }
        blockFlush(s, block);
    }
}

void helpCommand(Session& s, int argc, char** argv) {
    if (argc == 0) {
        Block block;
        blockReset(block);
        for (int i = 0; i < kCommandCount; i++)
            blockAdd(s, block, commands[i].usage);
        blockFlush(s, block);
        return;
    }
    for (int i = 0; i < kCommandCount; i++) {
        if (strcmp(commands[i].name, argv[0]))
            continue;
        reply(s, commands[i].usage);
        return;
    }
    reply(s, "Perintah tidak dikenal");
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
    prepareChatLines(chat);
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

// Resolves the fields of the bubble a reply lands in: the message list, the last
// message and the label it owns. A missing field only leaves the prefab's line
// cap in place, so the commands keep working.
void resolveMsgLabel() {
    if (!resolveField(classOf("Net__Multi__Chat"), "msgs", chatMsgs))
        return;
    void* listClass = fieldClass(chatMsgs);
    if (!resolveField(listClass, "_items", msgItems) || !resolveField(listClass, "_size", msgCount))
        return;
    void* msgClass = fieldElementClass(msgItems);
    if (!msgClass || !resolveField(msgClass, "c", msgObj))
        return;
    if (!resolveField(fieldClass(msgObj), "content__Text__Standard__Text__Label", msgLabel))
        return;
    resolveField(fieldClass(msgLabel), "mMaxLineCount", labelMaxLines);
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
        !resolveField(guestClass, "account", guestAccount))
        return false;
    if (!resolveField(fieldClass(guestAccount), "user_Name", accountName))
        return false;
    if (!resolveField(fieldClass(managerPlayer), "phy", playerPhy))
        return false;

    if (!resolveField(fieldClass(playerPhy), "pos", physicsPos))
        return false;
    if (!resolveField(fieldClass(physicsPos), "world", posWorld))
        return false;

    if (!resolveField(fieldClass(managerMulti), "transfer", multiTransfer))
        return false;
    void* transferClass = fieldClass(multiTransfer);
    if (!resolveField(transferClass, "pipe", transferPipe) ||
        !resolveField(transferClass, "host", transferHost))
        return false;

    // The guest manager frame callback drives the timed shake, so its manager
    // pointer is resolved the same way as every other field.
    if (!resolveField(classOf("Guest__Manager"), "a", guestManagerA))
        return false;

    // Replies and the local half of /close degrade on their own: a missing field
    // sends the answer to a toast and skips the local leave, never the commands.
    if (!resolveField(fieldClass(managerMulti), "_disconnect", multiDisconnect) ||
        !resolveField(fieldClass(multiDisconnect), "_state", disconnectState) ||
        !resolveField(fieldClass(disconnectState), "host", disconnectHost))
        LOGE("Close fields missing; /close skips the leave");
    if (resolveField(classOf("Net__Multi__Chat"), "_msg", chatMsg) &&
        resolveField(fieldClass(chatMsg), "sender", msgSender)) {
        resolveField(fieldClass(msgSender), "self", senderSelf);
        resolveField(fieldClass(msgSender), "other", senderOther);
    }

    // The name the game prints for this player, used when an answer is added
    // under the "other" sender id so it keeps the left layout.
    if (resolveField(classOf("Net__Multi__Chat"), "_text", chatText))
        resolveField(fieldClass(chatText), "me", textMe);

    // The template label that measures every bubble, used to lift its line cap
    // before the first multi-line answer. A missing field only leaves the cap.
    if (resolveField(classOf("Net__Multi__Chat"), "ui", chatUi) &&
        resolveField(classOf("UI__Manager"), "measure__Chat__Text__Label", chatMeasure))
        resolveField(fieldClass(chatMeasure), "mMaxLineCount", labelMaxLines);
    resolveMsgLabel();
    return true;
}

} // namespace

void installFullControlHooks() {
    ready = resolveFields() && target(T_CHAT_SEND).addr && target(T_CHAT_CLEAR).addr &&
            target(T_INPUT_SEND).addr && target(T_PLAYER_KEY).addr && target(T_KICK_SEND).addr &&
            target(T_ROOM_CLOSE_SEND).addr && target(T_EXPLOSION_SHAKE_SEND).addr &&
            target(T_HOST_KEY).addr && target(T_GUEST_UPDATE).addr;
    if (!ready) {
        LOGE("Full control fields missing; chat commands stay off");
        return;
    }
    LOGI("Full control fields OK");
    installHook(target(T_CHAT_SEND), reinterpret_cast<void*>(sendHook));
    installHook(target(T_INPUT_SEND), reinterpret_cast<void*>(sendButtonHook));
    installHook(target(T_PLAYER_KEY), reinterpret_cast<void*>(keyHook));
    installHook(target(T_GUEST_UPDATE), reinterpret_cast<void*>(updateHook));
}
