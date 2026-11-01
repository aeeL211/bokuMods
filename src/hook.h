#pragma once

#include "targets.h"

// Thin ShadowHook wrapper. Every hook goes through here so a failure is logged
// in one place and the caller can leave that feature on vanilla behavior.
bool initShadowHook();
bool installHook(Target& t, void* replacement, const char* label);
