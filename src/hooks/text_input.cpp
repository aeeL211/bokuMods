#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field batasInput = {};
Field dataNama = {};
Field batasNama = {};

// Game menulis ulang UIInput.characterLimit (nama 38, hari 2, bulan 2, tahun 4)
// tepat sebelum setiap edit, jadi kedua titik pemotongan harus menimpanya:
// Insert(string) menegakkan batas per ketukan, Validate(string) memangkas nilai
// saat Set/kirim.
void* hookValidasi(void* self, void* value, void* method) {
    auto asli = reinterpret_cast<void* (*)(void*, void*, void*)>(target(T_INPUT_VALIDATE).orig);
    if (!asli)
        return value;
    tulisInt(self, batasInput, INT32_MAX);
    return asli(self, value, method);
}

void hookSisip(void* self, void* text, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_INPUT_INSERT).orig);
    if (!asli)
        return;
    tulisInt(self, batasInput, INT32_MAX);
    asli(self, text, method);
}

// Keyboard mengubah string ini ke UTF-8 dan menelusuri karakternya sampai
// panjang di header-nya. String yang header-nya tidak lagi menyimpan panjang
// wajar membuat penelusuran itu melewati heap, jadi serahkan string kosong baru
// alih-alih membiarkan engine memindai memori yang tidak terpetakan.
constexpr int32_t kBatasKeyboard = 4096;

void* stringKosong() { return stringUtf16Baru(reinterpret_cast<const uint16_t*>(u""), 0); }

// Parameter Length tidak terpakai di jalur mobile, tetapi jalur Steam/gamepad
// meneruskannya ke input teks miliknya sendiri. Nol berarti tanpa batas karakter
// di sana.
void hookBukaKeyboard(void* self, int32_t s, int32_t type, void* text, int32_t, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, int32_t, int32_t, void*, int32_t, void*)>(
        target(T_KEYBOARD_OPEN).orig);
    if (!asli)
        return;
    int32_t panjang = panjangString(text);
    // Menyebut string yang akan diubah engine, supaya pointer buruk muncul di log
    // tepat sebelum crash, bukan hanya di alamat kesalahan.
    LOGI("Keyboard: s=%d type=%d text=%p panjang=%d", s, type, text, panjang);
    if (!text || panjang < 0 || panjang > kBatasKeyboard) {
        void* kosong = stringKosong();
        if (kosong)
            text = kosong;
    }
    asli(self, s, type, text, 0, method);
}

// Name__Check memangkas nama profil sesuai _name.length_Limit dan kembali ke
// nilai bawaan bila hasilnya kosong. Naikkan batas itu di sekitar panggilan
// supaya pemangkasannya lolos dan sisa pemeriksaannya tetap berjalan.
void hookCekNama(void* self, void** name, bool fix, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void**, bool, void*)>(target(T_NAME_CHECK).orig);
    if (!asli)
        return;
    void* data = bacaPtr(self, dataNama);
    int32_t tersimpan = 0;
    if (data) {
        tersimpan = bacaInt(data, batasNama);
        tulisInt(data, batasNama, INT32_MAX);
    }
    asli(self, name, fix, method);
    if (data)
        tulisInt(data, batasNama, tersimpan);
}

} // namespace

void pasangHookInputTeks() {
    if (!cariField(kelasDari("UIInput"), "characterLimit", batasInput)) {
        LOGE("Field UIInput.characterLimit tidak ditemukan; batas input tetap bawaan");
    }

    void* kelasAkun = kelasDari("Net__Account__Manager");
    if (cariField(kelasAkun, "_name", dataNama) &&
        cariField(kelasField(dataNama), "length_Limit", batasNama)) {
        LOGI("Batas nama field OK");
    } else {
        LOGE("Batas nama field tidak ditemukan; nama profil tetap terbatas");
    }

    pasangHook(target(T_INPUT_VALIDATE), reinterpret_cast<void*>(hookValidasi));
    pasangHook(target(T_INPUT_INSERT), reinterpret_cast<void*>(hookSisip));
    pasangHook(target(T_KEYBOARD_OPEN), reinterpret_cast<void*>(hookBukaKeyboard));
    pasangHook(target(T_NAME_CHECK), reinterpret_cast<void*>(hookCekNama));
}
