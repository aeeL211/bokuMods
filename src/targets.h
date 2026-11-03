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
    T_HH_CHANGE,
    T_EAT_END_KNEEL,
    T_EAT_END_SIT_F,
    T_EAT_END_SIT_S,
    T_EAT_END_FOOD,
    T_EAT_END_SUSHI_KNEEL,
    T_EAT_END_SUSHI_SIT_F,
    T_EAT_END_SUSHI_SIT_S,
    T_EAT_END_SUSHI_FOOD,
    T_EAT_END_BUN_KNEEL,
    T_EAT_END_BUN_SIT_F,
    T_EAT_END_BUN_SIT_S,
    T_EAT_END_BUN_FOOD,
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
    T_PRIVACY_LABEL,
    T_GUEST_COMMON,
    T_GUEST_PRIVACY_EMAIL,
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
    T_SLOT_LOAD,
    T_STATE_UPDATE,
    T_GUN_ACTION_END,
    T_ROCK_ACTION_END,
    T_ROCK_END_TO_ACTION,
    T_ROCK_ACTION,
    T_ROCK_END_DIR,
    T_TUMBLE_EXPLODE,
    T_FECES_CHARGE_STD,
    T_FECES_CHARGE_UP,
    T_FECES_ACTION_STD,
    T_FECES_ACTION_UP,
    T_FECES_CHARGE_UPDATE,
    T_FECES_UPDATE,
    T_USER_SET,
    T_USER_SET_LOCAL,
    T_USER_SET_LOCAL_ARRAY,
    T_USER_SET_LOCAL_LIST,
    T_GUEST_NAME,
    T_DATA_MODEL_NAME,
    T_COUNT,
};

struct Target {
    const char* cls;
    const char* name;
    int argc;
    void* addr;
    void* method;
    void* orig;
    // Class name of the last parameter, for overloads that share name and arity.
    const char* paramType = nullptr;
};

Target& target(TargetId id);

// Resolves every target by class name, method name and parameter count.
// Returns how many were found, which is T_COUNT on a healthy startup.
int scan();
