# Boku Mods

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Mod ini membuka batasan yang membuat
progres terasa lambat, lewat hook pada method IL2CPP. File game tidak diubah.

## Masalah yang diselesaikan

- **IAP berbayar** — pembelian disimulasikan, tidak perlu Play Billing.
- **Konten terkunci** — semua tile, model, dan melody langsung terbuka.
- **Nama profil terpotong** — batas input nama dinaikkan.
- **Tanggal lahir dibatasi** — hari dan tahun tidak lagi dipotong.
- **Host tidak mengizinkan save world** — save tetap bisa dilakukan.
- **Room ber-password** — bisa dimasuki tanpa mengetik password.
- **Posisi skin sempit** — slider X/Y diperlebar ke -20..20.
- **Gingerbread cepat habis** — kue tetap di tangan setelah 3 gigitan.
- **Kemampuan host terkunci** — panel host terbuka juga di room orang lain, jadi kick, izin
  pemain, build/destroy, save world, dan pengaturan room bisa dipakai.
- **Skin pemain lain tidak tersimpan** — setiap skin yang tampil disimpan otomatis ke file
  bernomor.
- **Detail akun disembunyikan** — setelan sembunyikan milik pemain lain diabaikan, jadi detail
  akun selalu terlihat.
- **Versi tidak jelas** — label versi diberi akhiran "(modded)".

## Pemasangan

Letakkan `libfebrian.so` di folder library proses game, lalu muat lewat loader/injector.
ShadowHook sudah ikut di dalam file itu, jadi tidak ada library tambahan yang perlu disalin.

## Log

Catatan ada di `Android/data/com.pixticle.bokuboku/files/bokumods.log`. Satu file dipakai terus
dan ditambahi tiap sesi, jadi riwayat tidak hilang.

## Catatan

- Hanya `arm64-v8a`.
- Skin tersimpan di `Android/data/com.pixticle.bokuboku/files/Skin/skin_1.txt` dan seterusnya,
  berurutan dari nomor kosong pertama.
- Gunakan di akun sendiri; mod ini bisa melanggar ketentuan layanan game.
- Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
