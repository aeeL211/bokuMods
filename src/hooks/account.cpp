#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field fieldAkun = {};
Field fieldPerkenalan = {};
Field fieldPanjang = {};

// Introduce__Set memotong teks sesuai _introduce.length sebelum menyimpannya.
// Menaikkan batas itu membuat seluruh teks ikut tersimpan.
void hookPerkenalan(void* self, void* text, void* method) {
    if (fieldAkun.field && fieldPerkenalan.field && fieldPanjang.field) {
        void* akun = bacaPtr(self, fieldAkun);
        void* perkenalan = bacaPtr(akun, fieldPerkenalan);
        if (perkenalan)
            tulisInt(perkenalan, fieldPanjang, INT32_MAX);
    }

    auto asli = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_INTRODUCE_SET).orig);
    if (asli)
        asli(self, text, method);
}

bool cariFieldAkun() {
    return cariField(kelasDari("Net__Account__My_Account__Detail"), "account", fieldAkun) &&
           cariField(kelasField(fieldAkun), "_introduce", fieldPerkenalan) &&
           cariField(kelasField(fieldPerkenalan), "length", fieldPanjang);
}

} // namespace

void pasangHookAkun() {
    if (cariFieldAkun()) {
        LOGI("Field akun OK");
    } else {
        LOGE("Field akun tidak ditemukan; perkenalan tetap terbatas");
    }

    pasangHook(target(T_INTRODUCE_SET), reinterpret_cast<void*>(hookPerkenalan));
}
