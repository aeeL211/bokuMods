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

// The toast is one short line; a longer name is cut at a character boundary and
// marked with an ellipsis so the message stays readable.
constexpr size_t kMaxNameLen = 64;
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
    if (toastClass)
        env->DeleteLocalRef(toastClass);
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
    if (looperClass)
        env->DeleteLocalRef(looperClass);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return looper != nullptr;
}

void* toastThread(void* arg) {
    char text[384];
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

// A name file can come from any editor, so it is read as bytes first and never
// rejected for its encoding. Valid UTF-8 is kept as-is, a UTF-16 byte order mark
// switches to a UTF-16 decode, and every other byte is read as Latin-1 so a
// legacy file still shows readable characters instead of an empty toast.
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

uint32_t readCodePoint(const char* s, size_t len) {
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    if (len == 1)
        return p[0];
    if (len == 2)
        return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if (len == 3)
        return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
}

size_t encodeUtf8(uint32_t code, char* out) {
    if (code < 0x80) {
        out[0] = static_cast<char>(code);
        return 1;
    }
    if (code < 0x800) {
        out[0] = static_cast<char>(0xC0 | (code >> 6));
        out[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (code >> 12));
        out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (code >> 18));
    out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (code & 0x3F));
    return 4;
}

struct NameBuffer {
    char* out;
    size_t size;
    size_t used;
    size_t chars;
};

// Control characters carry no visible name, so they are skipped instead of
// aborting the read. Returns false once the name is long enough to be cut.
bool addChar(NameBuffer& buf, uint32_t code) {
    if (code < 0x20 || code == 0x7F)
        return true;
    if (buf.chars == kMaxNameLen || buf.used + 4 + sizeof(kEllipsis) > buf.size)
        return false;
    buf.used += encodeUtf8(code, buf.out + buf.used);
    buf.chars++;
    return true;
}

// Returns true when the name was cut before the last UTF-16 unit.
bool readUtf16(const unsigned char* s, size_t len, bool little, NameBuffer& buf) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint32_t unit = little ? (s[i] | (s[i + 1] << 8)) : ((s[i] << 8) | s[i + 1]);
        uint32_t code = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            uint32_t low = 0;
            if (i + 3 < len) {
                const unsigned char* next = s + i + 2;
                low = little ? (next[0] | (next[1] << 8)) : ((next[0] << 8) | next[1]);
            }
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            } else {
                code = 0xFFFD;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            code = 0xFFFD;
        }
        if (!addChar(buf, code))
            return true;
    }
    return false;
}

// Returns true when the name was cut before the last character. Invalid UTF-8
// is read byte by byte as Latin-1, which keeps a Windows-1252 name readable.
bool readBytes(const char* s, size_t len, NameBuffer& buf) {
    for (size_t i = 0; i < len;) {
        size_t seq = utf8Length(s + i, len - i);
        uint32_t code = seq ? readCodePoint(s + i, seq) : static_cast<unsigned char>(s[i]);
        i += seq ? seq : 1;
        if (!addChar(buf, code))
            return true;
    }
    return false;
}

// The name file belongs next to the installed game, but an earlier build kept
// it in the <package>.mods dir, which is also the game's own dir once the clone
// takes that name. Both are tried, so either layout of the file is read.
bool userNamePath(char* out, size_t size, bool modsDir) {
    char root[512];
    if (!externalStorageRoot(root, sizeof(root)))
        return false;
    char package[192];
    if (!appPackage(package, sizeof(package)))
        return false;
    snprintf(out, size, "%s/Android/data/%s%s/files/Setting/User_Name.txt", root, package,
             modsDir ? ".mods" : "");
    return true;
}

// Read at toast time instead of caching: the file is small, the toast is shown
// once per session, and an edited name is picked up on the next launch.
bool readNameFile(const char* path, char* out, size_t size) {
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

    NameBuffer buf = {out, size, 0, 0};
    const auto* bytes = reinterpret_cast<const unsigned char*>(raw + begin);
    size_t len = end - begin;
    bool cut = false;
    if (len >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
        cut = readUtf16(bytes + 2, len - 2, true, buf);
    else if (len >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
        cut = readUtf16(bytes + 2, len - 2, false, buf);
    else
        cut = readBytes(raw + begin, len, buf);

    if (cut) {
        size_t dots = sizeof(kEllipsis) - 1;
        memcpy(buf.out + buf.used, kEllipsis, dots);
        buf.used += dots;
    }
    buf.out[buf.used] = '\0';
    LOGI("User name loaded: %s", buf.out);
    return true;
}

bool readName(char* out, size_t size) {
    char path[900];
    if (!userNamePath(path, sizeof(path), false)) {
        LOGE("User name location unavailable");
        return false;
    }
    if (readNameFile(path, out, size))
        return true;
    if (!userNamePath(path, sizeof(path), true))
        return false;
    return readNameFile(path, out, size);
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
    char name[320];
    if (!readName(name, sizeof(name))) {
        toast("MODS: Logged in");
        return;
    }
    char text[384];
    snprintf(text, sizeof(text), "MODS: Logged in as %s", name);
    toast(text);
}
