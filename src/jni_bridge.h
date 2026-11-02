#pragma once

#include <jni.h>

#include <cstddef>

// JNI is used by the log path, the toasts and the version suffix, so the VM
// handle and the UnityPlayer class live outside the logging guard.
JavaVM* javaVm();
void setJavaVm(JavaVM* vm);

jclass unityPlayer();
void cacheUnityPlayer(JNIEnv* env);

JavaVM* findJavaVm();
bool attachJni(JNIEnv** env, bool* detach);

// getExternalStorageDirectory is the only root that also works for paths owned
// by another app, so both the log fallback and the name file use it.
bool externalStorageRoot(char* out, size_t size);

// The package name comes from /proc/self/cmdline instead of being written into
// the source, so a renamed or cloned game still finds its own files.
bool appPackage(char* out, size_t size);
