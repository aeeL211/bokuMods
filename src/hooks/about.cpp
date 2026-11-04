#include "hooks.h"

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

// Setiap label versi yang bisa dibuka pemain diisi dari salah satu metode ini:
// halaman Tentang, layar judul, dan halaman bantuan multiplayer. Akhiran
// ditambahkan ke teks bawaan, jadi build ini ditandai modded tanpa
// menyembunyikan versi yang dilaporkan game.
constexpr char kAkhiranMod[] = " (modded)";

// String versi itu pendek; yang lebih panjang berarti ada sesuatu yang tidak
// terduga di ujungnya, jadi dibiarkan apa adanya daripada disalin mentah.
constexpr int32_t kBatasVersi = 240;

using HookVersi = void* (*)(void*, void*);

bool adaTeks(void* str, const char* jarum) {
    const uint16_t* karakter = karakterString(str);
    int32_t panjang = panjangString(str);
    size_t ukuranJarum = strlen(jarum);
    if (!karakter || static_cast<size_t>(panjang) < ukuranJarum)
        return false;
    for (int32_t i = 0; i + static_cast<int32_t>(ukuranJarum) <= panjang; i++) {
        size_t j = 0;
        while (j < ukuranJarum && karakter[i + j] == static_cast<uint16_t>(jarum[j]))
            j++;
        if (j == ukuranJarum)
            return true;
    }
    return false;
}

// Mengembalikan teks bawaan dengan kAkhiranMod ditambahkan, atau teks bawaan itu
// sendiri bila sudah membawa akhiran, kosong, atau terlalu panjang untuk sebuah
// label versi.
void* tandaiVersi(void* bawaan, const char* label) {
    if (!bawaan || !karakterString(bawaan))
        return bawaan;
    if (adaTeks(bawaan, kAkhiranMod))
        return bawaan;

    int32_t panjang = panjangString(bawaan);
    const uint16_t* karakter = karakterString(bawaan);
    if (!karakter || panjang <= 0 || panjang > kBatasVersi)
        return bawaan;

    uint16_t buffer[kBatasVersi + sizeof(kAkhiranMod)];
    int32_t terpakai = 0;
    for (int32_t i = 0; i < panjang; i++)
        buffer[terpakai++] = karakter[i];
    for (size_t i = 0; i + 1 < sizeof(kAkhiranMod); i++)
        buffer[terpakai++] = static_cast<uint16_t>(kAkhiranMod[i]);

    void* ditandai = stringUtf16Baru(buffer, terpakai);
    if (!ditandai)
        return bawaan;
    LOGI("%s: akhiran versi ditambahkan, panjang=%d", label, terpakai);
    return ditandai;
}

HookVersi asliDari(TargetId id) { return reinterpret_cast<HookVersi>(target(id).orig); }

void* hookTentang(void* self, void* method) {
    HookVersi asli = asliDari(T_ABOUT_VERSION);
    return asli ? tandaiVersi(asli(self, method), "Tentang") : nullptr;
}

void* hookJudul(void* self, void* method) {
    HookVersi asli = asliDari(T_TITLE_VERSION);
    return asli ? tandaiVersi(asli(self, method), "Judul") : nullptr;
}

void* hookBantuan(void* self, void* method) {
    HookVersi asli = asliDari(T_HELP_VERSION);
    return asli ? tandaiVersi(asli(self, method), "Bantuan") : nullptr;
}

} // namespace

void pasangHookTentang() {
    pasangHook(target(T_ABOUT_VERSION), reinterpret_cast<void*>(hookTentang));
    pasangHook(target(T_TITLE_VERSION), reinterpret_cast<void*>(hookJudul));
    pasangHook(target(T_HELP_VERSION), reinterpret_cast<void*>(hookBantuan));
}
