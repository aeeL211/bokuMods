#pragma once

// Checks the library name, decrypts the target table and verifies the build's
// integrity hash. Returns false when the environment does not match the build,
// in which case the caller must leave the game untouched.
bool initProtection();

// Checks the process list and the executable mappings. Returns false when a
// process or library that is not on the friend list is present.
bool protectionCheck();

// Repeats protectionCheck() in the background and removes every hook when the
// check fails.
void startProtectionWatchdog();
