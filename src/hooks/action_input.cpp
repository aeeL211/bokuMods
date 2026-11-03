#include "hooks.h"

#include "il2cpp_api.h"
#include "log.h"

namespace {

Field inputField = {};
Field inputManagerField = {};
Field actionField = {};
Field keepField = {};

} // namespace

// The interaction button is held while its input object keeps the action.
bool actionHeld(void* manager) {
    void* input = readPtr(manager, inputField);
    void* inputManager = readPtr(input, inputManagerField);
    void* action = readPtr(inputManager, actionField);
    return action && readBool(action, keepField);
}

// Shared by every feature that repeats its action while the button is held.
void resolveActionInput() {
    void* managerClass = classOf("Player__Manager");
    bool ok = resolveField(managerClass, "input", inputField) &&
              resolveField(fieldClass(inputField), "input", inputManagerField) &&
              resolveField(fieldClass(inputManagerField), "action", actionField) &&
              resolveField(fieldClass(actionField), "keep", keepField);
    if (ok)
        LOGI("Action input fields OK");
    else
        LOGE("Action input fields not found; held-button repeats stay off");
}
