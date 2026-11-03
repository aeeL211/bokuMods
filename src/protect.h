#pragma once

// Checks the library name, decrypts the target table and verifies the build's
// integrity hash. Returns false when the environment does not match the build,
// in which case the caller must leave the game untouched.
bool initProtection();
