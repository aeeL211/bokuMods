#pragma once

#include <jni.h>

#include <cstddef>

// JNI dipakai oleh jalur log, toast dan sufiks versi, jadi pegangan VM dan kelas
// UnityPlayer berada di luar guard pencatatan.
JavaVM* vmJava();
void setelVmJava(JavaVM* vm);

jclass kelasUnityPlayer();
void simpanKelasUnityPlayer(JNIEnv* env);

JavaVM* cariVmJava();
bool lekatJni(JNIEnv** env, bool* lepas);

// getExternalStorageDirectory adalah satu-satunya akar yang juga bekerja untuk
// path milik aplikasi lain, jadi cadangan log dan berkas nama sama-sama memakainya.
bool akarPenyimpananEksternal(char* out, size_t size);

// Nama paket diambil dari /proc/self/cmdline, bukan ditulis ke dalam sumber, jadi
// game yang diganti nama atau dikloning tetap menemukan berkasnya sendiri.
bool paketAplikasi(char* out, size_t size);
