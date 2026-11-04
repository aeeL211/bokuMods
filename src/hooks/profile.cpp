#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "log.h"
#include "targets.h"

namespace {

// Field hari dan bulan terbuka dengan 2 karakter, tahun dengan 4.
int32_t hookPanjangLahir(void*, int32_t, void*) { return INT32_MAX; }

// Birthday__Refresh_ hanya menampilkan nilai bila V <= Limit. Batas bulan, hari,
// dan tahun bawaan semuanya dilonggarkan, jadi angka apa pun yang diketik tetap
// tampil di layar.
void hookLahir(void* self, int32_t v, int32_t i, int32_t batas, void* bawaan, void* method) {
    LOGI("Tanggal lahir: v=%d i=%d batas=%d", v, i, batas);
    if (batas == 12 || batas == 31 || batas == 9999)
        batas = INT32_MAX;
    auto asli = reinterpret_cast<void (*)(void*, int32_t, int32_t, int32_t, void*, void*)>(
        target(T_BIRTHDAY_REFRESH).orig);
    if (!asli)
        return;
    asli(self, v, i, batas, bawaan, method);
}

// Date_Check(ref Year, ref Month, ref Day) menolkan tahun di atas 9999 dan
// mengatur ulang hari yang tidak ada di bulan itu, yang membuat field tanggal
// lahir terbatas. Lewati hanya saat field tanggal lahir sendiri sedang dikirim,
// supaya pemanggil lain tetap memakai validasi tanggal bawaan.
thread_local bool sedangUbahLahir = false;

void hookAkhirLahir(void* self, void* text, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void*, void*)>(target(T_BIRTHDAY_END).orig);
    if (!asli)
        return;
    sedangUbahLahir = true;
    asli(self, text, method);
    sedangUbahLahir = false;
}

void hookCekTanggal(int32_t* year, int32_t* month, int32_t* day, void* method) {
    if (sedangUbahLahir) {
        // Tidak ada yang dijepit di sini: hari dan bulan bebas memang tujuan
        // bypass ini, dan labelnya berupa teks, jadi tidak ada array yang
        // diindeks dengan nilai itu.
        LOGI("Tanggal lahir: pemeriksaan dilewati y=%d m=%d d=%d", year ? *year : -1,
             month ? *month : -1, day ? *day : -1);
        return;
    }
    auto asli =
        reinterpret_cast<void (*)(int32_t*, int32_t*, int32_t*, void*)>(target(T_DATE_CHECK).orig);
    if (!asli)
        return;
    asli(year, month, day, method);
}

} // namespace

void pasangHookProfil() {
    pasangHook(target(T_BIRTHDAY_REFRESH), reinterpret_cast<void*>(hookLahir));
    pasangHook(target(T_BIRTHDAY_LENGTH), reinterpret_cast<void*>(hookPanjangLahir));
    pasangHook(target(T_BIRTHDAY_END), reinterpret_cast<void*>(hookAkhirLahir));
    pasangHook(target(T_DATE_CHECK), reinterpret_cast<void*>(hookCekTanggal));
}
