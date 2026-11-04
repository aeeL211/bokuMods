#pragma once

// Pencatatan ke berkas bersifat opsional: -DBOKU_LOG=OFF membuang setiap panggilan
// log dan penangan crash, yang menghasilkan .so paling kecil.
#ifdef BOKU_LOG

void cetakLog(char tingkat, const char* fmt, ...);
void bukaFileLog();
void pasangLogCrash();

// Mencatat pemanggil abort() dan __stack_chk_fail() supaya SIGABRT berikutnya
// menyebut kode yang memutuskan mematikan proses. Panggil setelah ShadowHook siap.
void pasangProbeCrash();

// Path lengkap berkas yang ditulis sesi ini, atau nullptr sebelum dibuka.
const char* pathFileLog();

#define LOGI(...) cetakLog('I', __VA_ARGS__)
#define LOGE(...) cetakLog('E', __VA_ARGS__)

#else

inline void bukaFileLog() {}
inline void pasangLogCrash() {}
inline void pasangProbeCrash() {}
inline const char* pathFileLog() { return nullptr; }

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)

#endif
