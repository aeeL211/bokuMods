#include "info.h"

#include <cstddef>
#include <cstdio>

#include "jni_bridge.h"
#include "log.h"

#ifndef BOKU_BUILD_COMMIT
#define BOKU_BUILD_COMMIT "dev"
#endif

namespace {

bool versiAplikasi(char* nama, size_t size, jint& kode) {
    JNIEnv* env = nullptr;
    bool lepas = false;
    if (!lekatJni(&env, &lepas))
        return false;

    jobject activity = nullptr;
    jclass kelasActivity = nullptr;
    jobject manajer = nullptr;
    jclass kelasManajer = nullptr;
    jstring paket = nullptr;
    jobject informasi = nullptr;
    jclass kelasInformasi = nullptr;
    jstring versi = nullptr;
    bool berhasil = false;
    kode = 0;

    if (kelasUnityPlayer()) {
        jfieldID fieldAktivitas = env->GetStaticFieldID(kelasUnityPlayer(), "currentActivity",
                                                        "Landroid/app/Activity;");
        if (fieldAktivitas)
            activity = env->GetStaticObjectField(kelasUnityPlayer(), fieldAktivitas);
    }
    if (activity) {
        kelasActivity = env->GetObjectClass(activity);
        if (kelasActivity) {
            jmethodID getManager = env->GetMethodID(kelasActivity, "getPackageManager",
                                                    "()Landroid/content/pm/PackageManager;");
            if (getManager)
                manajer = env->CallObjectMethod(activity, getManager);
            jmethodID getPackage =
                env->GetMethodID(kelasActivity, "getPackageName", "()Ljava/lang/String;");
            if (getPackage)
                paket = static_cast<jstring>(env->CallObjectMethod(activity, getPackage));
        }
    }
    if (manajer && paket) {
        kelasManajer = env->GetObjectClass(manajer);
        if (kelasManajer) {
            jmethodID getInfo =
                env->GetMethodID(kelasManajer, "getPackageInfo",
                                 "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
            if (getInfo)
                informasi = env->CallObjectMethod(manajer, getInfo, paket, 0);
        }
    }
    if (informasi) {
        kelasInformasi = env->GetObjectClass(informasi);
        if (kelasInformasi) {
            jfieldID fieldNama = env->GetFieldID(kelasInformasi, "versionName",
                                                 "Ljava/lang/String;");
            jfieldID fieldKode = env->GetFieldID(kelasInformasi, "versionCode", "I");
            if (fieldNama)
                versi = static_cast<jstring>(env->GetObjectField(informasi, fieldNama));
            if (fieldKode)
                kode = env->GetIntField(informasi, fieldKode);
            if (versi) {
                const char* karakter = env->GetStringUTFChars(versi, nullptr);
                if (karakter) {
                    snprintf(nama, size, "%s", karakter);
                    berhasil = true;
                    env->ReleaseStringUTFChars(versi, karakter);
                }
            }
        }
    }

    if (versi)
        env->DeleteLocalRef(versi);
    if (kelasInformasi)
        env->DeleteLocalRef(kelasInformasi);
    if (informasi)
        env->DeleteLocalRef(informasi);
    if (paket)
        env->DeleteLocalRef(paket);
    if (kelasManajer)
        env->DeleteLocalRef(kelasManajer);
    if (manajer)
        env->DeleteLocalRef(manajer);
    if (kelasActivity)
        env->DeleteLocalRef(kelasActivity);
    if (activity)
        env->DeleteLocalRef(activity);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (lepas)
        cariVmJava()->DetachCurrentThread();
    return berhasil;
}

} // namespace

void catatLingkungan() {
    char versi[64] = {};
    jint kode = 0;
    if (versiAplikasi(versi, sizeof(versi), kode))
        LOGI("Boku Boku %s (code %d)", versi, kode);
    else
        LOGI("Versi Boku Boku tidak terbaca");
    LOGI("Mod by Febrian/Ian (code aeeL), build %s", BOKU_BUILD_COMMIT);
}
