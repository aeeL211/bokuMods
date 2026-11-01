#pragma once

// File logging is optional: -DBOKU_LOG=OFF drops every log call and the crash
// handler, which is the smallest .so.
#ifdef BOKU_LOG

void logPrint(char level, const char* fmt, ...);
void logOpenFile();
void installCrashLog();

#define LOGI(...) logPrint('I', __VA_ARGS__)
#define LOGE(...) logPrint('E', __VA_ARGS__)

#else

inline void logOpenFile() {}
inline void installCrashLog() {}

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)

#endif
