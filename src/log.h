#pragma once

// File logging is optional: -DBOKU_LOG=OFF drops every log call and the crash
// handler, which is the smallest .so.
#ifdef BOKU_LOG

void logPrint(char level, const char* fmt, ...);
void logOpenFile();
void installCrashLog();

// Records the caller of abort() and __stack_chk_fail() so the next SIGABRT names
// the code that decided to kill the process. Call once ShadowHook is up.
void installCrashProbes();

// Full path of the file this session writes, or nullptr before it is open.
const char* logFilePath();

#define LOGI(...) logPrint('I', __VA_ARGS__)
#define LOGE(...) logPrint('E', __VA_ARGS__)

#else

inline void logOpenFile() {}
inline void installCrashLog() {}
inline void installCrashProbes() {}
inline const char* logFilePath() { return nullptr; }

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)

#endif
