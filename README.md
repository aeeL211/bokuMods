# Boku Mods

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Mod ini membuka batasan yang membuat
progres terasa lambat, lewat hook pada method IL2CPP. File game tidak diubah.

## Masalah yang diselesaikan

- **IAP berbayar** — pembelian disimulasikan, tidak perlu Play Billing.
- **Konten terkunci** — semua tile, model, dan melody langsung terbuka.
- **Slot penyimpanan terbatas** — jumlah slot model, wajah, portrait, foto, dunia, dan koleksi
  lain langsung dibuka sampai batas tertinggi game (9999), jadi tidak perlu membelinya satu per
  satu.
- **Nama profil terpotong** — batas input nama dinaikkan.
- **Tanggal lahir dibatasi** — hari, bulan, dan tahun tidak lagi dipotong.
- **Teks perkenalan terpotong** — batas panjang perkenalan dinaikkan.
- **Host tidak mengizinkan save world** — save tetap bisa dilakukan.
- **Room ber-password** — bisa dimasuki tanpa mengetik password.
- **Posisi skin sempit** — slider X/Y diperlebar ke -25..25.
- **Gingerbread cepat habis** — kue tetap di tangan setelah 3 gigitan.
- **Skin pemain lain tidak tersimpan** — setiap skin yang tampil disalin otomatis ke slot model
  kosong pertama, memakai penyimpanan model milik game sendiri, jadi skin itu langsung muncul
  di daftar model dan bisa dipakai. Nama pemain asalnya ikut ditulis ke slot itu, jadi skin
  siapa pun bisa dikenali.
- **Room besar tidak muncul** — batas ukuran world milik perangkat ini diabaikan, jadi room
  berukuran besar tetap tampil di daftar dan bisa dimasuki.
- **Pesan pribadi pemain lain tidak terbaca** — isi DM antar pemain di room ikut tampil di chat
  room dengan awalan `[PM -> penerima]` dan dicatat di log. DM yang ditujukan ke akun sendiri
  tetap hanya tampil di jendela pribadi, tidak dobel.
- **World baru terlalu sempit** — batas slider ukuran world dinaikkan ke preset terbesar game
  (600x300), jadi world baru bisa dibuat lebih luas dari batas perangkat ini.
- **Pistol dan blaster lambat** — selama tombol interaksi ditahan, tembakan berulang sendiri
  dengan jeda tetap yang lebih pendek, baik saat berdiri maupun saat berjalan, tanpa perlu
  menekannya lagi tiap kali.
- **Amunisi blaster lemah** — blaster selalu memakai amunisi terkuat.
- **Layar bergetar dan tile berpecah** — getaran kamera dan pecahan tile dimatikan.
- **Terpental saat ledakan** — ledakan tidak lagi membuat pemain terpental dan jatuh; tile dan
  efek ledakan tetap berjalan.
- **Feces hilang setelah dilempar** — feces coklat, hijau, dan pink tetap di tangan setelah
  dilempar, jadi bisa dilempar berkali-kali.
- **Detail akun disembunyikan** — setelan sembunyikan milik pemain lain diabaikan, jadi detail
  akun selalu terlihat. Email pemain lain tidak ikut diunduh game, jadi baris email di daftar
  teman, tambah teman, verifikasi, dan halaman profil menampilkan ID akun sebagai gantinya,
  bukan kotak kosong.
- **Kata kasar disensor** — semua pengecekan kata terlarang dilewati, jadi chat, nama, dan teks
  profil bisa memakai kata apa pun.
- **Versi tidak jelas** — label versi diberi akhiran "(modded)".

## Pemasangan

Letakkan `libfebrian.so` di folder library proses game, lalu muat lewat loader/injector.
ShadowHook sudah ikut di dalam file itu, jadi tidak ada library tambahan yang perlu disalin.

## Log

Setiap sesi menulis file sendiri di `Android/data/<package>/files/`, dengan nama
`bokumods-YYYYMMDD-HHMMSS.log` mengikuti waktu lokal saat file dibuka. Package mengikuti game
yang terpasang; APK asli memakai `com.pixticle.bokuboku`, salinan mod memakai
`com.pixticle.bokuboku.mods`. Sesi lama tidak ditimpa dan tidak dihapus, jadi tiap kali game
dijalankan akan ada satu file baru, dan file dengan waktu paling akhir adalah sesi terakhir.
Kalau dua sesi terbuka pada detik yang sama, file kedua diberi akhiran `-2`.

## Catatan

- Hanya `arm64-v8a`.
- Skin tersimpan sebagai slot model game, yaitu
  `Android/data/<package>/files/Save/Model/<nomor>/Data.txt`, berurutan dari nomor kosong
  pertama. Isi tiap `Data.txt` sama dengan yang ditulis game saat menyimpan model sendiri.
  Slot yang sudah ada dibaca sekali di awal, jadi skin yang sudah pernah tersimpan tidak
  diduplikasi. Slot tanpa gambar pratinjau tetap bisa dibuka dan dipakai. Nama pemain asalnya
  ditulis ke `Name.txt` di folder slot yang sama begitu profil pemain itu diterima game; kalau
  profilnya tidak pernah datang, slot tetap tersimpan tanpa nama.
- Kemampuan host seperti kick, izin pemain, dan pengaturan room diperiksa di sisi server, jadi
  tidak dimodifikasi dan tetap mengikuti aturan room.
- Batas ukuran world memakai preset terbesar game, yaitu 600x300. World sebesar itu tetap butuh
  RAM; kalau perangkat kehabisan memori saat memuat world raksasa, game tetap bisa menutup
  dirinya sendiri.
- Isi pesan pribadi hanya versi teks; pesan suara pribadi tidak ditampilkan. Kalau pengirim
  sudah keluar room saat pesan masuk, chat room menolak nama itu dan hanya log yang mencatat.
  Setiap DM yang tampil juga bisa memicu bunyi notifikasi chat.
- Sensor kata hanya dilewati di aplikasi yang memakai mod; klien tanpa mod tetap menyaring kata
  yang dikirim maupun diterimanya.
- Gunakan di akun sendiri; mod ini bisa melanggar ketentuan layanan game.
- Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
