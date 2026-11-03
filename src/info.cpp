#include "info.h"

#include <cstddef>
#include <cstdio>

#include "jni_bridge.h"
#include "log.h"

#ifndef BOKU_BUILD_COMMIT
#define BOKU_BUILD_COMMIT "dev"
#endif

namespace {

bool appVersion(char* name, size_t size, jint& code) {
    JNIEnv* env = nullptr;
    bool detach = false;
    if (!attachJni(&env, &detach))
        return false;

    jobject activity = nullptr;
    jclass activityClass = nullptr;
    jobject manager = nullptr;
    jclass managerClass = nullptr;
    jstring package = nullptr;
    jobject info = nullptr;
    jclass infoClass = nullptr;
    jstring version = nullptr;
    bool ok = false;
    code = 0;

    if (unityPlayer()) {
        jfieldID current =
            env->GetStaticFieldID(unityPlayer(), "currentActivity", "Landroid/app/Activity;");
        if (current)
            activity = env->GetStaticObjectField(unityPlayer(), current);
    }
    if (activity) {
        activityClass = env->GetObjectClass(activity);
        if (activityClass) {
            jmethodID getManager =
                env->GetMethodID(activityClass, "getPackageManager",
                                 "()Landroid/content/pm/PackageManager;");
            if (getManager)
                manager = env->CallObjectMethod(activity, getManager);
            jmethodID getPackage =
                env->GetMethodID(activityClass, "getPackageName", "()Ljava/lang/String;");
            if (getPackage)
                package = static_cast<jstring>(env->CallObjectMethod(activity, getPackage));
        }
    }
    if (manager && package) {
        managerClass = env->GetObjectClass(manager);
        if (managerClass) {
            jmethodID getInfo = env->GetMethodID(
                managerClass, "getPackageInfo",
                "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
            if (getInfo)
                info = env->CallObjectMethod(manager, getInfo, package, 0);
        }
    }
    if (info) {
        infoClass = env->GetObjectClass(info);
        if (infoClass) {
            jfieldID nameField = env->GetFieldID(infoClass, "versionName", "Ljava/lang/String;");
            jfieldID codeField = env->GetFieldID(infoClass, "versionCode", "I");
            if (nameField)
                version = static_cast<jstring>(env->GetObjectField(info, nameField));
            if (codeField)
                code = env->GetIntField(info, codeField);
            if (version) {
                const char* chars = env->GetStringUTFChars(version, nullptr);
                if (chars) {
                    snprintf(name, size, "%s", chars);
                    ok = true;
                    env->ReleaseStringUTFChars(version, chars);
                }
            }
        }
    }

    if (version)
        env->DeleteLocalRef(version);
    if (infoClass)
        env->DeleteLocalRef(infoClass);
    if (info)
        env->DeleteLocalRef(info);
    if (package)
        env->DeleteLocalRef(package);
    if (managerClass)
        env->DeleteLocalRef(managerClass);
    if (manager)
        env->DeleteLocalRef(manager);
    if (activityClass)
        env->DeleteLocalRef(activityClass);
    if (activity)
        env->DeleteLocalRef(activity);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (detach)
        findJavaVm()->DetachCurrentThread();
    return ok;
}

} // namespace

void logEnvironment() {
    char version[64] = {};
    jint code = 0;
    if (appVersion(version, sizeof(version), code))
        LOGI("Boku Boku %s (code %d)", version, code);
    else
        LOGI("Boku Boku version not readable");
    LOGI("Mod by Febrian/Ian (code aeeL), build %s", BOKU_BUILD_COMMIT);
}
