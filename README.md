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
- **Skin pemain lain tidak tersimpan** — setiap skin yang tampil disalin otomatis ke slot model
  kosong pertama, memakai penyimpanan model milik game sendiri, jadi skin itu langsung muncul
  di daftar model dan bisa dipakai.
- **Room besar tidak muncul** — batas ukuran world milik perangkat ini diabaikan, jadi room
  berukuran besar tetap tampil di daftar dan bisa dimasuki.
- **World baru terlalu sempit** — batas slider ukuran world dinaikkan ke preset terbesar game
  (600x300), jadi world baru bisa dibuat lebih luas dari batas perangkat ini.
- **Detail akun disembunyikan** — setelan sembunyikan milik pemain lain diabaikan, jadi detail
  akun selalu terlihat.
- **Versi tidak jelas** — label versi diberi akhiran "(modded)".

## Pemasangan

Letakkan `libfebrian.so` di folder library proses game, lalu muat lewat loader/injector.
ShadowHook sudah ikut di dalam file itu, jadi tidak ada library tambahan yang perlu disalin.

## Log

Catatan ada di `Android/data/<package>/files/bokumods.log`, mengikuti package game yang
terpasang. APK asli memakai `com.pixticle.bokuboku`, salinan mod memakai
`com.pixticle.bokuboku.mods`. Satu file dipakai terus dan ditambahi tiap sesi selama aplikasi
di-update tanpa di-uninstall; memasang ulang dari awal menghapus folder data itu, jadi riwayat
log ikut hilang.

## Catatan

- Hanya `arm64-v8a`.
- Skin tersimpan sebagai slot model game, yaitu
  `Android/data/<package>/files/Save/Model/<nomor>/Data.txt`, berurutan dari nomor kosong
  pertama. Isi tiap `Data.txt` sama dengan yang ditulis game saat menyimpan model sendiri.
  Slot yang sudah ada dibaca sekali di awal, jadi skin yang sudah pernah tersimpan tidak
  diduplikasi. Slot tanpa gambar pratinjau tetap bisa dibuka dan dipakai.
- Kemampuan host seperti kick, izin pemain, dan pengaturan room diperiksa di sisi server, jadi
  tidak dimodifikasi dan tetap mengikuti aturan room.
- Batas ukuran world memakai preset terbesar game, yaitu 600x300. World sebesar itu tetap butuh
  RAM; kalau perangkat kehabisan memori saat memuat world raksasa, game tetap bisa menutup
  dirinya sendiri.
- Gunakan di akun sendiri; mod ini bisa melanggar ketentuan layanan game.
- Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
