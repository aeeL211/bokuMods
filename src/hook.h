#pragma once

#include "targets.h"

// Thin ShadowHook wrapper. Every hook goes through here so a failure is logged
// in one place and the caller can leave that feature on vanilla behavior.
bool initShadowHook();
bool installHook(Target& t, void* replacement);

// Hooks a bare address instead of a target entry. The crash probes use it for
// libc functions, which no target names, and it stays out of the hook count.
bool installProbeHook(void* addr, void* replacement, void** original, const char* name);

// Number of hooks that are currently installed.
int installedHooks();
