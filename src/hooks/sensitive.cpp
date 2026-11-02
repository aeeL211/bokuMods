#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Every text check in the game is decided here: the word list is asked first,
// and when the caller also asked for a note it is raised. Answering false skips
// both, so chat lines, names and profile text all pass.
bool sensitiveHook(void*, void*, int32_t, bool, void*) { return false; }

} // namespace

void installSensitiveHooks() {
    installHook(target(T_SENSITIVE_WORD), reinterpret_cast<void*>(sensitiveHook),
                "UI__Note.Term__Sensitive_Word");
}
