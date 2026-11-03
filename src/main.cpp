#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "hook.h"
#include "hooks/hooks.h"
#include "il2cpp_api.h"
#include "jni_bridge.h"
#include "log.h"
#include "targets.h"
#include "toast.h"

#if !defined(__aarch64__)
#error "Only arm64-v8a is supported"
#endif

namespace {

void* worker(void*) {
    LOGI("Worker start");
    if (!initShadowHook())
        return nullptr;

    for (int i = 0; i < 120 && !dlopen("libil2cpp.so", RTLD_LAZY | RTLD_NOLOAD); i++) {
        sleep(1);
    }
    sleep(2);

    if (!bindApi()) {
        LOGE("IL2CPP API not found");
        return nullptr;
    }
    // Unity installs its own handlers during startup, so take over only after it
    // has finished and chain back to it from the crash handler.
    installCrashLog();
    LOGI("IL2CPP API bound");

    void* dom = nullptr;
    for (int i = 0; i < 60 && !(dom = il2cppApi().domain()); i++) {
        sleep(1);
    }
    if (!dom) {
        LOGE("IL2CPP domain not ready");
        return nullptr;
    }

    LOGI("Domain ready, scanning");
    il2cppApi().attach(dom);
    int found = scan();
    LOGI("Scan done: found %d of %d methods", found, T_COUNT);

    installIapHooks();
    installMultiplayerHooks();
    installChatHooks();
    installSensitiveHooks();
    installTextInputHooks();
    installProfileHooks();
    installPasswordHooks();
    installSkinRangeHooks();
    installAboutHooks();
    installFoodHooks();
    installPrivacyHooks();
    installAccountHooks();
    installWeaponHooks();
    installEffectHooks();
    installKnockbackHooks();
    installFecesHooks();
    installSkinHooks();
    installSlotHooks();
    loginToast();
    return nullptr;
}

} // namespace

// Exported on purpose: ART looks up JNI_OnLoad in the library it just loaded,
// so a hidden symbol would fail the load.
extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM* vm, void*) {
    setJavaVm(vm);
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        cacheUnityPlayer(env);
    }
    logOpenFile();
    return JNI_VERSION_1_6;
}

__attribute__((constructor)) static void onLoad() {
    logOpenFile();
    LOGI("Library loaded");
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGE("Thread create failed");
    }
}
