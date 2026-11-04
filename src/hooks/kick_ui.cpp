#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

using FnPanel = void (*)(void*, void*);
using FnTermMulti = bool (*)(void*, void*, int32_t, bool*, void*);
using FnOrder = void (*)(void*, void*);
using FnNext = int32_t (*)(int32_t, void*);
using FnButtonNew = void (*)(void*, void*, int32_t, void*);

// Kedua panel tamu punya susunan field yang sama, jadi satu tabel kecil cukup
// menyimpan kelas dan target yang berbeda di antara keduanya.
struct Panel {
    const char* kelas;
    TargetId init;
    TargetId ikon;
    TargetId teks;
    TargetId kirim;
    Field multi;
    Field menu;
    Field buttons;
    Field tombol;
    Field idTombol;
    Field idTendang;
};

Panel panelConsole = {"Net__Multi__Guest__Main__Console", T_KICK_CONSOLE_INIT, T_KICK_CONSOLE_ICON,
                      T_KICK_CONSOLE_TEXT,          T_KICK_CONSOLE_YES,  {}, {}, {}, {}, {}, {}};
Panel panelMenu = {"Net__Multi__Guest__Menu", T_KICK_MENU_INIT, T_KICK_MENU_ICON, T_KICK_MENU_TEXT,
                   T_KICK_MENU_YES,          {}, {}, {}, {}, {}, {}};

Field fieldNet = {};
Field fieldState = {};
Field fieldStatus = {};
Field fieldHost = {};
Field fieldGuest = {};
Field fieldConsole = {};
Field fieldGuestMenu = {};
Field fieldManajer = {};
Field fieldCmn = {};
Field fieldTemp = {};
Field fieldTempId = {};
Field fieldMultiManajer = {};

void* alamatField(void* objek, const Field& bidang) {
    return reinterpret_cast<char*>(objek) + bidang.offset;
}

int32_t idTendang(void* panel, const Panel& p) {
    void* tombol = bacaPtr(panel, p.tombol);
    void* id = tombol ? bacaPtr(tombol, p.idTombol) : nullptr;
    return id ? bacaInt(id, p.idTendang) : 0;
}

// Id tombol yang sedang ditekan disimpan game di Input__Cmn sebelum panel
// dipanggil, dan setiap cabang panel dibuka hanya untuk id miliknya sendiri.
int32_t idSentuh(void* sendiri) {
    void* cmn = bacaPtr(sendiri, fieldCmn);
    void* temp = cmn ? bacaPtr(cmn, fieldTemp) : nullptr;
    return temp ? bacaInt(temp, fieldTempId) : 0;
}

void* multiDari(void* sendiri) {
    void* manajer = bacaPtr(sendiri, fieldManajer);
    return manajer ? bacaPtr(manajer, fieldMultiManajer) : nullptr;
}

// Panel tamu melewati tombol tendang karena cabangnya hanya dibuka untuk host.
// Satu tombol ditambahkan di belakang tombol bawaan dengan status host sesaat,
// lalu statusnya dikembalikan supaya sisa panel tetap berperilaku sebagai tamu.
void tambahTombol(void* panel, const Panel& p) {
    void* multi = bacaPtr(panel, p.multi);
    void* net = multi ? bacaPtr(multi, fieldNet) : nullptr;
    void* status = net ? bacaPtr(net, fieldStatus) : nullptr;
    void* menu = bacaPtr(panel, p.menu);
    void* tombol = bacaPtr(panel, p.tombol);
    void* id = tombol ? bacaPtr(tombol, p.idTombol) : nullptr;
    if (!net || !status || !menu || !id)
        return;

    int32_t nilaiHost = bacaInt(status, fieldHost);
    int32_t stateAsli = bacaInt(net, fieldState);
    if (stateAsli == nilaiHost)
        return;

    auto order = reinterpret_cast<FnOrder>(target(T_ORDER).addr);
    auto next = reinterpret_cast<FnNext>(target(T_NEXT).addr);
    auto baru = reinterpret_cast<FnButtonNew>(target(T_BUTTON_NEW).addr);
    auto ikon = reinterpret_cast<FnPanel>(target(p.ikon).addr);
    auto teks = reinterpret_cast<FnPanel>(target(p.teks).addr);
    if (!order || !next || !baru || !ikon || !teks)
        return;

    int32_t idBaru = 0;
    tulisInt(net, fieldState, nilaiHost);
    order(&idBaru, target(T_ORDER).method);
    tulisInt(id, p.idTendang, idBaru);
    baru(menu, alamatField(panel, p.buttons), next(idBaru, target(T_NEXT).method),
         target(T_BUTTON_NEW).method);
    ikon(panel, target(p.ikon).method);
    teks(panel, target(p.teks).method);
    tulisInt(net, fieldState, stateAsli);
}

bool tombolTendang(void* multi, int32_t id) {
    void* guest = bacaPtr(multi, fieldGuest);
    if (!guest)
        return false;
    void* console = bacaPtr(guest, fieldConsole);
    if (console && idTendang(console, panelConsole) == id)
        return true;
    void* menu = bacaPtr(guest, fieldGuestMenu);
    return menu && idTendang(menu, panelMenu) == id;
}

// Pengiriman tendang hanya diterima penerima bila kuncinya kunci host, jadi
// status host dan kunci itu dipakai selama satu panggilan tombol konfirmasi.
void kirimTendang(TargetId id, void* panel, const Panel& p, void* metode) {
    mulaiSpoof(bacaPtr(panel, p.multi));
    auto asli = reinterpret_cast<FnPanel>(target(id).orig);
    if (asli)
        asli(panel, metode);
    hentiSpoof();
}

void hookInitConsole(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<FnPanel>(target(T_KICK_CONSOLE_INIT).orig);
    if (asli)
        asli(sendiri, metode);
    tambahTombol(sendiri, panelConsole);
}

void hookInitMenu(void* sendiri, void* metode) {
    auto asli = reinterpret_cast<FnPanel>(target(T_KICK_MENU_INIT).orig);
    if (asli)
        asli(sendiri, metode);
    tambahTombol(sendiri, panelMenu);
}

bool hookTermMulti(void* sendiri, void* objek, int32_t t, bool* e, void* metode) {
    void* multi = multiDari(sendiri);
    void* net = multi ? bacaPtr(multi, fieldNet) : nullptr;
    void* status = net ? bacaPtr(net, fieldStatus) : nullptr;
    int32_t id = idSentuh(sendiri);
    bool tendang = net && status && id > 0 && tombolTendang(multi, id);

    int32_t stateAsli = 0;
    if (tendang) {
        stateAsli = bacaInt(net, fieldState);
        tulisInt(net, fieldState, bacaInt(status, fieldHost));
    }
    auto panggil = reinterpret_cast<FnTermMulti>(target(T_INPUT_TERM_MULTI).orig);
    bool hasil = panggil ? panggil(sendiri, objek, t, e, metode) : false;
    if (tendang)
        tulisInt(net, fieldState, stateAsli);
    return hasil;
}

void hookYesConsole(void* sendiri, void* metode) {
    kirimTendang(T_KICK_CONSOLE_YES, sendiri, panelConsole, metode);
}

void hookYesMenu(void* sendiri, void* metode) {
    kirimTendang(T_KICK_MENU_YES, sendiri, panelMenu, metode);
}

bool siapkanPanel(Panel& p) {
    void* kelas = kelasDari(p.kelas);
    if (!kelas)
        return false;
    if (!cariField(kelas, "multi", p.multi) || !cariField(kelas, "menu", p.menu) ||
        !cariField(kelas, "buttons", p.buttons) || !cariField(kelas, "_button", p.tombol))
        return false;
    void* kelasTombol = kelasField(p.tombol);
    if (!cariField(kelasTombol, "_id", p.idTombol))
        return false;
    void* kelasId = kelasField(p.idTombol);
    return cariField(kelasId, "kick", p.idTendang);
}

bool cariFieldTendang() {
    if (!siapkanPanel(panelConsole) || !siapkanPanel(panelMenu))
        return false;

    void* kelasMulti = kelasDari("Net__Multi");
    if (!cariField(kelasMulti, "_net", fieldNet) || !cariField(kelasMulti, "guest", fieldGuest))
        return false;
    void* kelasNet = kelasField(fieldNet);
    if (!cariField(kelasNet, "state", fieldState) || !cariField(kelasNet, "_state", fieldStatus))
        return false;
    if (!cariField(kelasField(fieldStatus), "host", fieldHost))
        return false;
    void* kelasGuest = kelasField(fieldGuest);
    if (!cariField(kelasGuest, "main_Console", fieldConsole) ||
        !cariField(kelasGuest, "_menu", fieldGuestMenu))
        return false;

    void* kelasInput = kelasDari("Input__Began__Net");
    if (!cariField(kelasInput, "a", fieldManajer) || !cariField(kelasInput, "cmn", fieldCmn))
        return false;
    if (!cariField(kelasField(fieldManajer), "multi", fieldMultiManajer))
        return false;
    void* kelasCmn = kelasField(fieldCmn);
    if (!cariField(kelasCmn, "t", fieldTemp))
        return false;
    return cariField(kelasField(fieldTemp), "id", fieldTempId);
}

} // namespace

void pasangHookTendang() {
    if (!cariFieldTendang()) {
        LOGE("Field tombol tendang tidak ditemukan");
        return;
    }
    if (!target(T_ORDER).addr || !target(T_NEXT).addr || !target(T_BUTTON_NEW).addr) {
        LOGE("Metode tombol tendang tidak ditemukan");
        return;
    }
    LOGI("Field tombol tendang siap");
    pasangHook(target(T_KICK_CONSOLE_INIT), reinterpret_cast<void*>(hookInitConsole));
    pasangHook(target(T_KICK_MENU_INIT), reinterpret_cast<void*>(hookInitMenu));
    pasangHook(target(T_INPUT_TERM_MULTI), reinterpret_cast<void*>(hookTermMulti));
    pasangHook(target(T_KICK_CONSOLE_YES), reinterpret_cast<void*>(hookYesConsole));
    pasangHook(target(T_KICK_MENU_YES), reinterpret_cast<void*>(hookYesMenu));
}
