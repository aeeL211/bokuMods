#pragma once

void pasangHookIap();
void pasangHookMultiplayer();
void pasangHookChat();
void pasangHookSensitif();
void pasangHookInputTeks();
void pasangHookProfil();
void pasangHookSandi();
void pasangHookRentangSkin();
void pasangHookTentang();
void pasangHookMakanan();
void pasangHookSkin();
void pasangHookPrivasi();
void pasangHookAkun();
void pasangHookSenjata();
void pasangHookEfek();
void pasangHookTerpental();
void pasangHookKotoran();
void pasangHookTangan();
void pasangHookSlot();
void pasangHookKontrolPenuh();

// Bernilai benar selama tombol interaksi ditahan untuk pemain ini.
bool tombolAksiDitekan(void* manajer);
void cariFieldAksi();

// Dinaikkan oleh fitur yang menahan itemnya di tangan padahal panggilan stok
// akan mengosongkan slot tangan.
void setelTahanTangan(bool tahan);

// Mengembalikan id yang terakhir dipegang ke tangan setelah animasi makan
// menyembunyikan sprite-nya, jadi makanan yang ditahan tetap terlihat.
void segarkanTangan();
