#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field fieldDetail = {};
Field fieldSandi = {};
Field fieldStr = {};
Field fieldDataRoom = {};
Field fieldSandiRoom = {};

// Sandi room hanya beberapa karakter. Yang lebih panjang dari ini adalah objek
// basi yang header-nya dipakai ulang, bukan string sungguhan, dan meneruskannya
// ke keyboard membuat Unity memindai melewati ujung heap.
constexpr int32_t kBatasSandi = 4096;

bool stringLayak(void* str) {
    if (!str)
        return false;
    int32_t panjang = panjangString(str);
    return panjang >= 0 && panjang <= kBatasSandi;
}

void* stringKosong() { return stringUtf16Baru(reinterpret_cast<const uint16_t*>(u""), 0); }

// Data room memiliki sandi room, jadi membacanya saat dibutuhkan menjaga setiap
// pemakai nilai itu pada objek hidup yang sama.
void* sandiRoom(void* self) {
    void* detail = bacaPtr(self, fieldDetail);
    void* dataRoom = bacaPtr(detail, fieldDataRoom);
    return bacaPtr(dataRoom, fieldSandiRoom);
}

// Term__Bottom_Button() adalah status aktif tombol OK: fungsi itu melaporkan
// apakah _password.str berbeda dari string kosong. Input__Ok() langsung kembali
// bila hasilnya false, dan tombolnya sendiri diredupkan, sehingga kotak kosong
// tidak pernah bisa dikirim dan perbandingannya tidak pernah tercapai. Dengan
// hook ini tombolnya selalu bisa dipakai; sandi room tetap diperiksa kode asli,
// yang dipenuhi okHook tepat sebelum perbandingan.
bool hookTombol(void*, void*) { return true; }

// Input__Ok membandingkan _password.str dengan detail._room_Data.password dan
// baru setelah itu menutup keyboard serta memanggil Join__Start. Kotak tetap
// kosong, jadi objek string milik room itu sendiri dipasang untuk perbandingan:
// String.Equals mengembalikan true pada referensi yang sama sebelum membaca satu
// karakter pun, dan data room menjaga objeknya tetap hidup.
void hookOk(void* self, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, void*)>(target(T_PASSWORD_OK).orig);
    if (!asli)
        return;
    void* nilai = bacaPtr(self, fieldSandi);
    void* sandi = sandiRoom(self);
    if (nilai && stringLayak(sandi)) {
        setelRef(nilai, fieldStr, sandi);
        // Perbandingan membaca field ini setelah fungsi asli berjalan, jadi
        // penulisan yang tidak mendarat adalah satu-satunya kegagalan yang harus
        // terlihat jelas di log berikutnya.
        void* tersimpan = bacaPtr(nilai, fieldStr);
        if (tersimpan == sandi)
            LOGI("Sandi: sandi room terpasang, panjang=%d", panjangString(sandi));
        else
            LOGE("Sandi: sandi room gagal ditulis, ada=%p seharusnya=%p", tersimpan, sandi);
    } else {
        LOGE("Sandi: sandi room tidak ada, perbandingan berjalan apa adanya");
    }
    // Mencapai baris ini berarti perbandingannya gagal: jalur yang lolos
    // memanggil Join__Start sebagai tail call dan tidak pernah kembali.
    asli(self, method);
    LOGI("Sandi: perbandingan gerbang gagal, hookOk kembali");
}

// Jalur keyboard meneruskan _password.str sebagai teks awal, jadi null di sana
// membatalkan sebelum field terbuka. _Start biasanya menulis string kosong;
// setelah join, panel bisa masih memegang string yang dipakai ulang. Sandi room
// dipasang kembali untuk kasus itu, karena objeknya dimiliki data room dan tetap
// hidup selama keyboard membacanya, sedangkan alokasi baru bisa dikumpulkan
// terlalu cepat.
void hookField(void* self, bool touch, void* method) {
    auto asli = reinterpret_cast<void (*)(void*, bool, void*)>(target(T_PASSWORD_FIELD).orig);
    if (!asli)
        return;
    void* nilai = bacaPtr(self, fieldSandi);
    void* str = nilai ? bacaPtr(nilai, fieldStr) : nullptr;
    // Panel menyerahkan string ini ke keyboard, jadi catat sebelum fungsi asli
    // berjalan: pointer basi di sini yang kemudian membuat engine tersedak.
    LOGI("Field sandi: nilai=%p str=%p panjang=%d", nilai, str, panjangString(str));
    if (nilai && !stringLayak(str)) {
        void* sandi = sandiRoom(self);
        void* awal = stringLayak(sandi) ? sandi : stringKosong();
        if (awal)
            setelRef(nilai, fieldStr, awal);
    }
    asli(self, touch, method);
}

} // namespace

void pasangHookSandi() {
    void* kelasSandi = kelasDari("UI__Menu__Net__Multi__Join__Room_List__Detail__Password");
    if (kelasSandi && cariField(kelasSandi, "detail", fieldDetail) &&
        cariField(kelasSandi, "_password", fieldSandi) &&
        cariField(kelasField(fieldSandi), "str", fieldStr) &&
        cariField(kelasField(fieldDetail), "_room_Data", fieldDataRoom) &&
        cariField(kelasField(fieldDataRoom), "password", fieldSandiRoom)) {
        LOGI("Field sandi room OK");
        LOGI("Offset sandi: _password=%zu str=%zu room_Data=%zu room_password=%zu",
             fieldSandi.offset, fieldStr.offset, fieldDataRoom.offset, fieldSandiRoom.offset);
    } else {
        LOGE("Field sandi room tidak ditemukan; room bersandi tetap manual");
    }

    pasangHook(target(T_PASSWORD_OK), reinterpret_cast<void*>(hookOk));
    pasangHook(target(T_PASSWORD_FIELD), reinterpret_cast<void*>(hookField));
    pasangHook(target(T_PASSWORD_BUTTON), reinterpret_cast<void*>(hookTombol));
}
