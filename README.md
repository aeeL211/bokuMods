# Boku Mods

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Mod ini membuka batasan yang membuat
progres terasa lambat, lewat hook pada method IL2CPP. File game tidak diubah.

## Masalah yang diselesaikan

- **IAP berbayar** — pembelian disimulasikan, tidak perlu Play Billing.
- **Konten terkunci** — semua tile, model, dan melody langsung terbuka.
- **Slot penyimpanan terbatas** — jumlah slot model, wajah, portrait, foto, dunia, dan koleksi
  lain langsung dibuka tanpa batas (`INT32_MAX`), jadi tidak perlu membelinya satu per satu.
- **Nama profil terpotong** — batas input nama dinaikkan.
- **Tanggal lahir dibatasi** — hari, bulan, dan tahun tidak lagi dipotong.
- **Teks perkenalan terpotong** — batas panjang perkenalan dinaikkan.
- **Host tidak mengizinkan save world** — save tetap bisa dilakukan.
- **Room ber-password** — bisa dimasuki tanpa mengetik password.
- **Posisi skin sempit** — slider X/Y diperlebar ke -25..25.
- **Makanan cepat habis** — hot cross bun dan gingerbread tetap di tangan setelah beberapa
  gigitan, jadi bisa dimakan sebanyak yang diinginkan, dan modelnya langsung tampil lagi
  setelah makan. Sushi, es krim, dan popsicle sengaja dibiarkan habis seperti biasa, karena
  satu gigitan sudah menghabiskan itemnya. Makanan yang dimakan langsung dari piring tetap
  habis seperti vanilla.
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
- **Pistol dan buster lambat** — selama tombol interaksi ditahan, tembakan berulang sendiri
  dengan jeda tetap yang lebih pendek, baik saat berdiri maupun saat berjalan, tanpa perlu
  menekannya lagi tiap kali.
- **Amunisi buster lemah** — buster selalu memakai amunisi terkuat.
- **Layar bergetar dan tile berpecah** — getaran kamera dan pecahan tile dimatikan.
- **Terpental saat ledakan** — ledakan tidak lagi membuat pemain terpental dan jatuh; tile dan
  efek ledakan tetap berjalan.
- **Feces hilang setelah dilempar** — feces coklat, hijau, dan pink tetap di tangan setelah
  dilempar, otomatis terlempar lagi selama tombol interaksi ditahan, sama seperti pistol dan
  buster, jadi bisa dilempar berkali-kali. Setiap lemparan memakai kekuatan penuh, jadi jaraknya
  tidak lagi tergantung berapa lama tombol ditahan sebelum dilepas. Arah lemparan mengikuti
  bidikan saat tombol ditekan, jadi bisa lurus maupun ke atas.
- **Detail akun disembunyikan** — setelan sembunyikan milik pemain lain diabaikan, jadi detail
  akun selalu terlihat. Email pemain lain hanya ikut terkirim kalau pemain itu sendiri
  menyalakan opsi tampilkan email, jadi baris email di daftar teman, tambah teman, verifikasi,
  dan halaman profil menampilkan tulisan privasi email pemiliknya (umum, hanya teman, atau
  sembunyi) sebagai gantinya, bukan kotak kosong.
- **Kata kasar disensor** — semua pengecekan kata terlarang dilewati, jadi chat, nama, dan teks
  profil bisa memakai kata apa pun.
- **Versi tidak jelas** — label versi diberi akhiran "(modded)".
- **Kontrol room lewat chat** — perintah chat menjalankan aksi tingkat host: kick, tutup room,
  dan getar kamera. Jawaban perintah memakai bahasa Indonesia, dan baris perintah tidak pernah
  dikirim ke pemain lain.

## Perintah chat

Ketik perintah di kotak chat room, lalu kirim lewat tombol kirim atau aksi kirim di keyboard.
Baris perintah tidak pernah sampai ke pemain lain: jawabannya muncul di chat room perangkat
sendiri sebagai baris dari diri sendiri, dan kotak chat langsung dikosongkan. Jawaban memakai
bahasa Indonesia, sedangkan nama perintahnya tetap bahasa Inggris. Kalau objek pesan game tidak
tersedia, jawaban tampil sebagai toast. Teks yang diawali `/` tapi bukan perintah yang dikenal
tetap terkirim sebagai chat biasa.

Pemain disebut dengan nomor, bukan nama. `#1` selalu pemain sendiri, pemain lain menyusul `#2`,
`#3`, dan seterusnya. Nomor mengikuti daftar tamu room, jadi bisa bergeser kalau ada pemain
masuk atau keluar; cek ulang dengan `/list` sebelum memakai perintah lain.

| Perintah | Halaman | Fungsi |
|---|---|---|
| `/list` | 3 | daftar pemain dan nomornya, `#1` adalah diri sendiri |
| `/kick <n>` | 1 | keluarkan pemain nomor n dari room; host tidak bisa ditendang |
| `/kickall` | 1 | keluarkan semua tamu, host dan diri sendiri tetap di room |
| `/close` | 1 | tutup room lalu keluar; hanya host, tanpa konfirmasi |
| `/shake <detik>` | 2 | getarkan kamera paling kuat selama itu, paling lama 90 detik |
| `/status` | 3 | key sendiri, key host, status host, jumlah pemain |
| `/log` | 3 | lokasi file log sesi ini |
| `/help <perintah\|-a>` | 3 | penjelasan satu perintah, atau semua dengan `-a` |
| `/menu [1-3\|-a]` | 3 | daftar perintah per halaman, atau semua dengan `-a` |

`/menu` tanpa angka menampilkan halaman 1: kontrol room di halaman 1, efek di halaman 2, dan
utilitas di halaman 3; `/menu -a` mencetak ketiga halaman dan `/help -a` mencetak semua baris
bantuan. `/shake <detik>` memakai kekuatan getar terbesar yang game pakai saat ledakan, diulang
tiap 0,2 detik selama jangka waktu itu, dan berhenti tepat saat waktunya habis. Paket kick,
tutup room, dan getaran dikirim memakai key host room, jadi klien lain menerimanya sebagai
perintah dari host; pemain lain ikut bergetar kalau kameranya dekat dengan posisi pemain yang
mengetik perintah. `/close` hanya untuk host: paket tutup room dikirim ke semua tamu, lalu
perangkat host menjalankan alur keluar yang sama dengan tombol tutup room bawaan game, jadi room
benar-benar kosong dan hilang dari daftar. Tamu yang mengetik `/close` dijawab
`Hanya host yang bisa menutup room` dan tidak ada paket yang dikirim. Perintah lain hanya berlaku
di room yang sedang dimasuki, dan tamu tetap bisa memakai `/kick`, `/kickall`, dan `/shake`.
`/kickall` menendang semua tamu kecuali host, jadi room tetap hidup dan hanya tersisa host dan
diri sendiri; `/kick` yang menyasar nomor host dijawab `Host tidak bisa ditendang`. `/kickall`
menjawab `Menendang tamu satu per satu`, lalu menendang satu tamu tiap 0,25 detik, dan menutup
dengan toast `Kickall: N tamu ditendang`, karena beberapa paket dalam satu frame hanya yang
pertama yang sampai.

## Pemasangan

Letakkan `libfebrian.so` di folder library proses game, lalu muat lewat loader/injector.
ShadowHook sudah ikut di dalam file itu, jadi tidak ada library tambahan yang perlu disalin.
Nama file harus tetap `libfebrian.so`: salinan yang diganti namanya tidak bisa membuka tabel
target, jadi mod tidak aktif dan game berjalan seperti biasa.

## Log

Setiap sesi menulis file sendiri di `Android/data/<package>/files/`, dengan nama `bokumods-1.log`,
`bokumods-2.log`, dan seterusnya mengikuti urutan sesi. Baris `Log file:` di awal log hanya
menulis nama filenya, bukan path penuh. Package mengikuti game yang terpasang; APK asli memakai
`com.pixticle.bokuboku`, salinan mod memakai `com.pixticle.bokuboku.mods`. Sesi lama tidak
ditimpa dan tidak dihapus, jadi tiap kali game dijalankan akan ada satu file baru, dan nomor
paling besar adalah sesi terakhir. Setelah baris `Worker start`, log mencatat versi game dan
pembuat mod, misalnya `Boku Boku 1.0.317 (code 317)` dan
`Mod by Febrian/Ian (code aeeL), build 17467c3`.

## Catatan

- Hanya `arm64-v8a`.
- Mod memeriksa nama file dan keutuhan isi file sekali di awal. Kalau salah satu tidak cocok,
  mod tidak memasang hook dan game berjalan seperti biasa. Tidak ada pemeriksaan proses atau
  library saat game berjalan, jadi sesi yang sudah aktif tidak bisa kehilangan hook di tengah
  permainan.
- Skin tersimpan sebagai slot model game, yaitu
  `Android/data/<package>/files/Save/Model/<nomor>/Data.txt`, berurutan dari nomor kosong
  pertama. Isi tiap `Data.txt` sama dengan yang ditulis game saat menyimpan model sendiri.
  Slot yang sudah ada dibaca sekali di awal, jadi skin yang sudah pernah tersimpan tidak
  diduplikasi. Slot tanpa gambar pratinjau tetap bisa dibuka dan dipakai. Nama pemain asalnya
  ditulis ke `Name.txt` di folder slot yang sama begitu profil pemain itu diterima game; kalau
  profilnya tidak pernah datang, slot tetap tersimpan tanpa nama.
- Izin build dan destroy tetap ditentukan host room, jadi tamu tidak bisa mengubah tile di luar
  izin itu. Yang dikirim mod hanya paket kick, tutup room, dan getaran lewat perintah chat,
  memakai key host room sebagai pengirim.
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
