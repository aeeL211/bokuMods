#include "toast.h"

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "jni_bridge.h"
#include "log.h"

namespace {

// The login name is published by the mod manager app, not by the game, so it
// lives under that app's external files dir. getExternalFilesDir is tied to the
// calling package, so the path is built from the shared storage root instead.
constexpr char kNamePath[] = "Android/data/com.pixticle.bokuboku.mods/files/Setting/User_Name.txt";

// The toast is one short line; a longer name is cut at a character boundary and
// marked with an ellipsis so the message stays readable.
constexpr size_t kMaxNameLen = 20;
constexpr char kEllipsis[] = "...";

// Toast.show() needs a Looper on the calling thread: the toast view is driven
// by messages posted to that Looper, so a thread without one cannot show it.
// Hook callbacks run on the game thread, which has the main Looper, so they
// show the toast directly; the mod worker does not, so it gets a short-lived
// thread that prepares its own Looper and keeps it running.
void showToast(JNIEnv* env, const char* text) {
    jobject activity = nullptr;
    if (unityPlayer()) {
        jfieldID current =
            env->GetStaticFieldID(unityPlayer(), "currentActivity", "Landroid/app/Activity;");
        if (current)
            activity = env->GetStaticObjectField(unityPlayer(), current);
    }

    jclass toastClass = env->FindClass("android/widget/Toast");
    jmethodID make =
        toastClass ? env->GetStaticMethodID(toastClass, "makeText",
                                            "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
                                            "Landroid/widget/Toast;")
                   : nullptr;
    jstring message = env->NewStringUTF(text);
    jobject toast = (make && message && activity)
                        ? env->CallStaticObjectMethod(toastClass, make, activity, message, 0)
                        : nullptr;
    if (toast) {
        jmethodID show = env->GetMethodID(toastClass, "show", "()V");
        if (show)
            env->CallVoidMethod(toast, show);
        env->DeleteLocalRef(toast);
    }
    if (message)
        env->DeleteLocalRef(message);
    if (activity)
        env->DeleteLocalRef(activity);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("Toast failed: %s", text);
    }
}

bool hasLooper(JNIEnv* env) {
    jclass looperClass = env->FindClass("android/os/Looper");
    jmethodID my = looperClass
                       ? env->GetStaticMethodID(looperClass, "myLooper", "()Landroid/os/Looper;")
                       : nullptr;
    jobject looper = my ? env->CallStaticObjectMethod(looperClass, my) : nullptr;
    if (looper)
        env->DeleteLocalRef(looper);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return looper != nullptr;
}

void* toastThread(void* arg) {
    char text[160];
    snprintf(text, sizeof(text), "%s", static_cast<const char*>(arg));
    free(arg);

    JNIEnv* env = nullptr;
    bool detach = false;
    if (!attachJni(&env, &detach))
        return nullptr;

    jclass looperClass = env->FindClass("android/os/Looper");
    jmethodID prepare =
        looperClass ? env->GetStaticMethodID(looperClass, "prepare", "()V") : nullptr;
    if (prepare)
        env->CallStaticVoidMethod(looperClass, prepare);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    showToast(env, text);

    // Keeps the toast's own messages flowing on older Android versions, where the
    // view is shown and hidden through handlers on the creating thread.
    jmethodID loop = looperClass ? env->GetStaticMethodID(looperClass, "loop", "()V") : nullptr;
    if (loop)
        env->CallStaticVoidMethod(looperClass, loop);
    return nullptr;
}

// A stray control byte or invalid sequence would show up as garbage in the
// toast, so the file is rejected instead of being displayed as-is.
bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Returns the length of the UTF-8 sequence at s, or 0 when it is invalid.
size_t utf8Length(const char* s, size_t avail) {
    unsigned char first = static_cast<unsigned char>(s[0]);
    if (first < 0x80)
        return 1;
    size_t len = 0;
    uint32_t code = 0;
    if ((first & 0xE0) == 0xC0) {
        len = 2;
        code = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        len = 3;
        code = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        len = 4;
        code = first & 0x07;
    } else {
        return 0;
    }
    if (len > avail)
        return 0;
    for (size_t i = 1; i < len; i++) {
        unsigned char next = static_cast<unsigned char>(s[i]);
        if ((next & 0xC0) != 0x80)
            return 0;
        code = (code << 6) | (next & 0x3F);
    }
    if ((len == 2 && code < 0x80) || (len == 3 && code < 0x800) || (len == 4 && code < 0x10000)) {
        return 0;
    }
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        return 0;
    return len;
}

// Read at toast time instead of caching: the file is small, the toast is shown
// once per session, and an edited name is picked up on the next launch.
bool readName(char* out, size_t size) {
    char root[512];
    if (!externalStorageRoot(root, sizeof(root))) {
        LOGE("User name root unavailable");
        return false;
    }
    char path[768];
    snprintf(path, sizeof(path), "%s/%s", root, kNamePath);

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int err = errno;
        LOGE("User name file open failed: %s (%d %s)", path, err, strerror(err));
        return false;
    }
    char raw[512];
    ssize_t n = read(fd, raw, sizeof(raw) - 1);
    int err = errno;
    close(fd);
    if (n < 0) {
        LOGE("User name file read failed: %s (%d %s)", path, err, strerror(err));
        return false;
    }
    if (n == 0) {
        LOGE("User name file is empty: %s", path);
        return false;
    }

    size_t begin = 0;
    size_t end = static_cast<size_t>(n);
    while (begin < end && isSpace(static_cast<unsigned char>(raw[begin])))
        begin++;
    while (end > begin && isSpace(static_cast<unsigned char>(raw[end - 1])))
        end--;
    if (begin == end) {
        LOGE("User name file has no name: %s", path);
        return false;
    }

    size_t used = 0;
    size_t chars = 0;
    bool cut = false;
    for (size_t i = begin; i < end;) {
        size_t seq = utf8Length(raw + i, end - i);
        if (!seq) {
            LOGE("User name is not valid UTF-8: %s", path);
            return false;
        }
        unsigned char byte = static_cast<unsigned char>(raw[i]);
        if (seq == 1 && (byte < 0x20 || byte == 0x7F)) {
            LOGE("User name has a control character: %s", path);
            return false;
        }
        if (chars == kMaxNameLen || used + seq + sizeof(kEllipsis) > size) {
            cut = true;
            break;
        }
        memcpy(out + used, raw + i, seq);
        used += seq;
        chars++;
        i += seq;
    }
    if (cut) {
        size_t dots = sizeof(kEllipsis) - 1;
        memcpy(out + used, kEllipsis, dots);
        used += dots;
    }
    out[used] = '\0';
    LOGI("User name loaded: %s", out);
    return true;
}

} // namespace

void toast(const char* text) {
    JNIEnv* env = nullptr;
    bool detach = false;
    if (!attachJni(&env, &detach))
        return;

    if (hasLooper(env)) {
        showToast(env, text);
    } else {
        char* copy = strdup(text);
        if (copy) {
            pthread_t t;
            if (pthread_create(&t, nullptr, toastThread, copy) == 0) {
                pthread_detach(t);
            } else {
                free(copy);
            }
        }
    }
    if (detach)
        findJavaVm()->DetachCurrentThread();
}

// The activation toast names the account from the mod manager's file; a broken
// or missing file falls back to a plain login message instead of failing.
void loginToast() {
    char name[128];
    if (!readName(name, sizeof(name))) {
        toast("MODS: Logged in");
        return;
    }
    char text[192];
    snprintf(text, sizeof(text), "MODS: Logged in as %s", name);
    toast(text);
}
