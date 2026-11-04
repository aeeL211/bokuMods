#include "hooks.h"

#include "il2cpp_api.h"
#include "log.h"

namespace {

Field fieldInput = {};
Field fieldManajerInput = {};
Field fieldAksi = {};
Field fieldTahan = {};

} // namespace

// Tombol interaksi sedang ditahan selama objek input-nya menyimpan aksi.
bool tombolAksiDitekan(void* manager) {
    void* input = bacaPtr(manager, fieldInput);
    void* manajerInput = bacaPtr(input, fieldManajerInput);
    void* aksi = bacaPtr(manajerInput, fieldAksi);
    return aksi && bacaBool(aksi, fieldTahan);
}

// Dipakai bersama oleh setiap fitur yang mengulang aksinya selama tombol ditahan.
void cariFieldAksi() {
    void* kelasManajer = kelasDari("Player__Manager");
    bool ok = cariField(kelasManajer, "input", fieldInput) &&
              cariField(kelasField(fieldInput), "input", fieldManajerInput) &&
              cariField(kelasField(fieldManajerInput), "action", fieldAksi) &&
              cariField(kelasField(fieldAksi), "keep", fieldTahan);
    if (ok)
        LOGI("Field input aksi OK");
    else
        LOGE("Field input aksi tidak ditemukan; ulangan tombol tahan tetap mati");
}
