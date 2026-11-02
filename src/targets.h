#pragma once

// argc is the declared parameter count; the hidden MethodInfo* is not counted.
enum TargetId {
    T_BUY,
    T_PURCHASE_COMPLETE,
    T_UNLOCK_CHECK,
    T_PERMISSION_START,
    T_PERMISSION_SAVE,
    T_PASSWORD_OK,
    T_PASSWORD_FIELD,
    T_INPUT_VALIDATE,
    T_INPUT_INSERT,
    T_KEYBOARD_OPEN,
    T_NAME_CHECK,
    T_BIRTHDAY_REFRESH,
    T_BIRTHDAY_LENGTH,
    T_BIRTHDAY_END,
    T_DATE_CHECK,
    T_POS_REFRESH_STEP,
    T_PORTRAIT_REFRESH_STEP,
    T_ABOUT_VERSION,
    T_TITLE_VERSION,
    T_HELP_VERSION,
    T_PASSWORD_BUTTON,
    T_TILE_KEY,
    T_EAT_CHANGE_TERM,
    T_EAT_CHANGE_ADD,
    T_HH_CHANGE,
    T_EAT_END_KNEEL,
    T_EAT_END_SIT_F,
    T_EAT_END_SIT_S,
    T_EAT_END_FOOD,
    T_ROOM_SIZE,
    T_WORLD_CAP,
    T_GUEST_ADD,
    T_DATA_MODEL_WRITE,
    T_DATA_MODEL_PATH,
    T_PRIVACY_MANAGER,
    T_PRIVACY_BLACKLIST,
    T_PRIVACY_ADD_FRIEND,
    T_PRIVACY_FRIEND_PROFILE,
    T_PRIVACY_VERIFY,
    T_PRIVACY_VERIFY_EMAIL,
    T_ACCOUNT_EMAIL,
    T_GUN_END,
    T_ROCK_END_UPDATE,
    T_ROCK_ID,
    T_SHAKE,
    T_SHAKE_NUM,
    T_DEBRIS,
    T_DEBRIS_TILE,
    T_INTRODUCE_SET,
    T_GUEST_MSG_RECEIVE,
    T_CHAT_MSG_RECEIVE,
    T_PLAYER_KEY,
    T_SENSITIVE_WORD,
    T_GUN_UPDATE,
    T_ROCK_UPDATE,
    T_VERIFY_REFRESH,
    T_COUNT,
};

struct Target {
    const char* cls;
    const char* name;
    int argc;
    void* addr;
    void* method;
    void* orig;
};

Target& target(TargetId id);

// Resolves every target by class name, method name and parameter count.
// Returns how many were found, which is T_COUNT on a healthy startup.
int scan();
