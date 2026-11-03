#pragma once

#include <cstddef>

#include "targets_generated.h"

struct Target {
    const char* cls;
    const char* name;
    const char* paramType;
    int argc;
    void* addr;
    void* method;
    void* orig;
};

Target& target(TargetId id);

// Decrypts the target names with the basename of the library that was actually
// loaded. A renamed copy derives a different key, so the names come out wrong
// and scan() resolves nothing. Returns false when the basename is not the one
// the table was built for.
bool initTargets(const char* libraryName);

// Resolves every target by class name, method name and parameter count.
// Returns how many were found, which is T_COUNT on a healthy startup.
int scan();
