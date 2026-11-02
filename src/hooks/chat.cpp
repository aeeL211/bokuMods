#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

using ReceiveFn = void (*)(void*, void*, void*, void*, void*);
using RoomChatFn = void (*)(void*, void*, void*, void*);
using KeyFn = void* (*)(void*, void*);

// Every chat line is redrawn on each message, so a long body is cut at this
// many UTF-16 units instead of being copied whole.
constexpr size_t kMaxLineChars = 512;
constexpr size_t kMaxNameBytes = 64;
constexpr size_t kMaxLogBytes = 160;

Field managerField = {};
Field chatField = {};
Field playerField = {};
bool chatReady = false;

// A payload can carry control bytes, which would split a log line in two, so
// they are flattened to spaces.
void flatten(char* text) {
    for (; *text; text++) {
        if (static_cast<unsigned char>(*text) < 0x20)
            *text = ' ';
    }
}

const char* utf8Of(void* str, char* out, size_t size) {
    if (!toUtf8(str, out, size)) {
        out[0] = '\0';
        return out;
    }
    flatten(out);
    return out;
}

void logPrivate(void* user, void* receiver, void* text) {
    char userText[kMaxNameBytes];
    char receiverText[kMaxNameBytes];
    char bodyText[kMaxLogBytes];
    LOGI("Private message: %s -> %s: %s", utf8Of(user, userText, sizeof(userText)),
         utf8Of(receiver, receiverText, sizeof(receiverText)),
         utf8Of(text, bodyText, sizeof(bodyText)));
}

// The room chat already labels the sender, so only the receiver goes in front
// of the body. The prefix is ASCII and the rest stays UTF-16.
void* privateLine(void* receiver, void* text) {
    const uint16_t* name = stringChars(receiver);
    const uint16_t* body = stringChars(text);
    int32_t nameLen = stringLength(receiver);
    int32_t bodyLen = stringLength(text);
    if (!name || !body || nameLen <= 0 || bodyLen <= 0)
        return nullptr;

    uint16_t line[kMaxLineChars];
    size_t used = 0;
    for (const char* p = "[PM -> "; *p && used < kMaxLineChars; p++)
        line[used++] = static_cast<uint16_t>(*p);
    for (int32_t i = 0; i < nameLen && used < kMaxLineChars; i++)
        line[used++] = name[i];
    for (const char* p = "] "; *p && used < kMaxLineChars; p++)
        line[used++] = static_cast<uint16_t>(*p);
    for (int32_t i = 0; i < bodyLen && used < kMaxLineChars; i++)
        line[used++] = body[i];
    return newUtf16String(line, static_cast<int32_t>(used));
}

// The receiver check inside the game compares this exact value, so the same
// call decides whether a message already has a window of its own.
void* localName(void* self) {
    void* manager = readPtr(self, managerField);
    void* player = readPtr(manager, playerField);
    auto key = reinterpret_cast<KeyFn>(target(T_PLAYER_KEY).addr);
    if (!player || !key)
        return nullptr;
    return key(player, target(T_PLAYER_KEY).method);
}

bool sameText(void* a, void* b) {
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

// A private message is broadcast to every client and only the receiver check
// drops it, so the text is already here. It is logged, then replayed into the
// room chat under the sender's name with the receiver in the prefix and no
// packet of its own.
void receiveHook(void* self, void* receiver, void* user, void* text, void* method) {
    logPrivate(user, receiver, text);

    auto orig = reinterpret_cast<ReceiveFn>(target(T_GUEST_MSG_RECEIVE).orig);
    if (orig)
        orig(self, receiver, user, text, method);

    auto roomChat = reinterpret_cast<RoomChatFn>(target(T_CHAT_MSG_RECEIVE).addr);
    if (!chatReady || !roomChat)
        return;

    void* own = localName(self);
    if (own && sameText(own, receiver))
        return;

    void* chat = readPtr(self, chatField);
    void* line = privateLine(receiver, text);
    if (chat && line)
        roomChat(chat, user, line, target(T_CHAT_MSG_RECEIVE).method);
}

} // namespace

void installChatHooks() {
    void* chatClass = classOf("Net__Multi__Guest__Chat");
    chatReady = resolveField(chatClass, "chat", chatField);
    bool ownReady = resolveField(chatClass, "a", managerField) &&
                    resolveField(fieldClass(managerField), "player", playerField) &&
                    target(T_PLAYER_KEY).addr;

    if (chatReady && ownReady)
        LOGI("Chat fields OK");
    else if (!chatReady)
        LOGE("Chat field not found; private messages stay in the log");
    else
        LOGE("Own name not found; messages to this player appear twice");

    installHook(target(T_GUEST_MSG_RECEIVE), reinterpret_cast<void*>(receiveHook),
                "Guest__Chat.Msg__Receive__Text");
}
