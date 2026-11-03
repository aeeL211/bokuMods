#pragma once

#include "targets.h"

// Thin ShadowHook wrapper. Every hook goes through here so a failure is logged
// in one place and the caller can leave that feature on vanilla behavior.
bool initShadowHook();
bool installHook(Target& t, void* replacement);

// Number of hooks that are currently installed.
int installedHooks();

// Removes every installed hook. Used when the protection watchdog decides the
// process is no longer trusted, so the game falls back to vanilla behavior.
void unhookAll();
