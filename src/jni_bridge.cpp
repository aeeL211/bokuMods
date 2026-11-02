#include "jni_bridge.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace {

JavaVM* cachedVm = nullptr;
jclass playerClass = nullptr;

} // namespace

JavaVM* javaVm() { return cachedVm; }

void setJavaVm(JavaVM* vm) { cachedVm = vm; }

jclass unityPlayer() { return playerClass; }

void cacheUnityPlayer(JNIEnv* env) {
    jclass unity = env->FindClass("com.unity3d.player.UnityPlayer");
    if (unity)
        playerClass = static_cast<jclass>(env->NewGlobalRef(unity));
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

JavaVM* findJavaVm() {
    if (cachedVm)
        return cachedVm;
    typedef jint (*GetVms)(JavaVM**, jsize, jsize*);
    auto created = reinterpret_cast<GetVms>(dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (!created)
        return nullptr;
    JavaVM* vm = nullptr;
    jsize count = 0;
    if (created(&vm, 1, &count) != JNI_OK || count < 1)
        return nullptr;
    cachedVm = vm;
    return vm;
}

bool attachJni(JNIEnv** env, bool* detach) {
    JavaVM* vm = findJavaVm();
    if (!vm)
        return false;
    *detach = false;
    if (vm->GetEnv(reinterpret_cast<void**>(env), JNI_VERSION_1_6) == JNI_OK)
        return true;
    if (vm->AttachCurrentThread(env, nullptr) != JNI_OK)
        return false;
    *detach = true;
    return true;
}

bool externalStorageRoot(char* out, size_t size) {
    JNIEnv* env = nullptr;
    bool detach = false;
    if (!attachJni(&env, &detach))
        return false;

    bool ok = false;
    jclass envClass = env->FindClass("android/os/Environment");
    if (envClass) {
        jmethodID get =
            env->GetStaticMethodID(envClass, "getExternalStorageDirectory", "()Ljava/io/File;");
        if (get) {
            jobject root = env->CallStaticObjectMethod(envClass, get);
            if (root) {
                jclass fileClass = env->FindClass("java/io/File");
                jmethodID getPath = fileClass ? env->GetMethodID(fileClass, "getAbsolutePath",
                                                                 "()Ljava/lang/String;")
                                              : nullptr;
                jstring path =
                    getPath ? static_cast<jstring>(env->CallObjectMethod(root, getPath)) : nullptr;
                if (path) {
                    const char* chars = env->GetStringUTFChars(path, nullptr);
                    if (chars) {
                        snprintf(out, size, "%s", chars);
                        ok = true;
                        env->ReleaseStringUTFChars(path, chars);
                    }
                    env->DeleteLocalRef(path);
                }
                env->DeleteLocalRef(root);
            }
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (detach)
        findJavaVm()->DetachCurrentThread();
    return ok;
}

bool appPackage(char* out, size_t size) {
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    char buffer[192];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0)
        return false;
    buffer[n] = '\0';

    // A secondary process appends ":name" to the package, so only the part
    // before the colon belongs to the app dir.
    char* colon = strchr(buffer, ':');
    if (colon)
        *colon = '\0';
    size_t len = strlen(buffer);
    if (len == 0 || len >= size)
        return false;
    memcpy(out, buffer, len + 1);
    return true;
}
