# Boku Mods

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Mod ini melonggarkan batasan yang
membuat progres terasa lambat lewat hook pada method IL2CPP. Berkas game tidak diubah, dan
ShadowHook ikut di dalam satu berkas `.so`.

Hanya `arm64-v8a`. Tidak ada komponen Java, Xposed, atau LSPosed.

## Yang diubah

**Ekonomi dan konten**

- Pembelian IAP disimulasikan tanpa Play Billing, dan semua tile, model, serta melody langsung
  terbuka.
- Semua jenis slot penyimpanan (model, wajah, portrait, foto, dunia, dan koleksi lain) terbuka
  tanpa batas, jadi tidak perlu membeli slot satu per satu.

**Input dan profil**

- Batas nama profil, tanggal lahir, teks perkenalan, dan kotak sandi room dinaikkan, jadi
  isinya tidak terpotong.
- Slider posisi skin X/Y dan portrait X/Y melebar dari -5..5 menjadi -25..25.
- Kata terlarang tidak disaring lagi di perangkat ini: chat, nama, dan teks profil bisa memakai
  kata apa pun.

**Room dan dunia**

- Save world tetap bisa dilakukan walaupun host mematikan izinnya di awal.
- Room bersandi bisa dimasuki tanpa mengetik sandinya.
- Room yang ukuran dunianya di atas batas perangkat ini tetap muncul di daftar dan bisa
  dimasuki.
- Batas slider ukuran world dinaikkan ke preset terbesar game (600x300), jadi world baru bisa
  dibuat lebih luas dari batas perangkat ini.

**Pemain lain**

- Setelan sembunyikan milik pemain lain diabaikan, jadi detail akun selalu terlihat.
- Baris email yang disembunyikan pemiliknya tidak dibiarkan kosong: baris itu menampilkan
  tulisan privasi email pemiliknya (umum, hanya teman, atau sembunyi).
- Setiap skin pemain yang tampil disalin otomatis ke slot model kosong pertama memakai penulis
  model milik game sendiri, jadi skin itu langsung muncul di daftar model dan bisa dipakai.
  Nama pemain asalnya ikut disimpan, sehingga skin siapa pun bisa dikenali.
- Pesan pribadi antar pemain ikut tampil di chat room dengan awalan `[PM -> penerima]` dan
  dicatat di log. DM yang ditujukan ke akun sendiri tetap hanya tampil di jendela pribadi.

**Pertarungan dan barang**

- Selama tombol interaksi ditahan, pistol dan buster menembak berulang sendiri dengan jeda tetap
  yang lebih pendek, baik saat berdiri maupun berjalan.
- Buster selalu memakai amunisi terkuat.
- Feces coklat, hijau, dan pink tetap di tangan setelah dilempar, terlempar lagi selama tombol
  ditahan, dan setiap lemparan memakai kekuatan penuh. Arah lemparan mengikuti bidikan saat
  tombol ditekan, jadi bisa lurus maupun ke atas.
- Hot cross bun dan gingerbread tetap di tangan setelah beberapa gigitan dan modelnya langsung
  tampil lagi setelah makan.
- Getaran kamera dan pecahan tile dimatikan, dan ledakan tidak lagi membuat pemain terpental.

**Tampilan**

- Label versi di layar judul, halaman About, dan bantuan multiplayer diberi akhiran
  `(modded)`.
- RoomKit: perintah chat room untuk tendang, daftar pemain, dan info sesi.

## RoomKit

RoomKit adalah kumpulan perintah chat room. Ketik perintah di kotak chat room, lalu kirim lewat
tombol kirim atau aksi kirim di keyboard.
Baris perintah tidak pernah sampai ke pemain lain: jawabannya muncul di chat room perangkat
sendiri, di sisi kiri seperti pesan yang diterima, dengan nama pemain sendiri sebagai pengirim,
dan kotak chat langsung dikosongkan. Satu jawaban tampil dalam satu bubble, dan beberapa baris
jawaban digabung ke dalam bubble itu dengan pemisah baris baru, jadi `/list`, `/status`, dan
satu jawaban `/menu` terbaca sebagai satu pesan. Jawaban yang terlalu panjang dipotong menjadi
beberapa bubble pendek supaya tingginya tidak melebihi panel chat. Kalau objek pesan game tidak
tersedia, jawaban tampil sebagai toast. Teks yang diawali `/` tapi bukan perintah yang dikenal
tetap terkirim sebagai chat biasa.

Pemain disebut dengan nomor, bukan nama. `#1` selalu pemain sendiri, pemain lain menyusul `#2`,
`#3`, dan seterusnya. Nomor mengikuti daftar tamu room, jadi bisa bergeser kalau ada pemain
masuk atau keluar; cek ulang dengan `/list` sebelum memakai perintah lain.

| Perintah | Bagian | Fungsi |
|---|---|---|
| `/kick <n>` | Aksi room | keluarkan pemain nomor n dari room; host tidak bisa ditendang |
| `/kickall` | Aksi room | keluarkan semua tamu, host dan diri sendiri tetap di room |
| `/list` | Info | daftar pemain dan nomornya, `#1` adalah diri sendiri |
| `/status` | Info | kunci sendiri, kunci host, status host, jumlah pemain |
| `/log` | Info | lokasi berkas log sesi ini |
| `/menu` | Bantuan | ringkasan semua perintah dalam satu bubble |
| `/help [perintah]` | Bantuan | penjelasan satu perintah, atau semua per bagian |

`/menu` mencetak satu bubble: satu baris judul, lalu satu baris per bagian berisi nama
perintahnya. `/help` tanpa argumen mencetak rincian tiap bagian sebagai bubble sendiri, sedangkan
`/help kick` hanya baris perintah itu.

Paket kick dikirim memakai kunci host room, jadi klien lain menerimanya sebagai perintah dari
host. Perintah hanya berlaku di room yang sedang dimasuki, dan tamu tetap bisa memakai `/kick`
dan `/kickall`. `/kick` yang menyasar nomor host dijawab `Host tidak bisa ditendang`. `/kickall`
menjawab `Menendang tamu satu per satu`, lalu menendang satu tamu tiap 0,25 detik, karena
beberapa paket dalam satu frame hanya yang pertama yang sampai.

Argumen `/kick` harus angka. Teks yang tidak terbaca sebagai angka
ditolak dengan jawaban di chat, jadi salah ketik tidak menjadi nomor atau durasi lain.

## Pemasangan

Letakkan `libfebrian.so` di folder library proses game, lalu muat lewat loader atau injector.
ShadowHook sudah ikut di dalam berkas itu, jadi tidak ada library tambahan yang perlu disalin.
Nama berkas harus tetap `libfebrian.so`: salinan yang diganti namanya tidak bisa membuka tabel
target, jadi mod tidak aktif dan game berjalan seperti biasa.

## Log

Setiap sesi menulis berkas sendiri di `Android/data/<package>/files/`, dengan nama
`bokumods-1.log`, `bokumods-2.log`, dan seterusnya mengikuti urutan sesi. Package mengikuti game
yang terpasang; APK asli memakai `com.pixticle.bokuboku`, salinan mod memakai
`com.pixticle.bokuboku.mods`. Sesi lama tidak ditimpa dan tidak dihapus, jadi tiap kali game
dijalankan akan ada satu berkas baru, dan nomor paling besar adalah sesi terakhir.

Awal log mencatat versi game dan pembuat mod, misalnya `Boku Boku 1.0.317 (code 317)` dan
`Mod by Febrian/Ian (code aeeL), build 17467c3`, lalu `ShadowHook siap`,
`Scan selesai: 90 dari 90 metode ditemukan`, dan `Hook terpasang: 74`. Baris `Berkas log:`
hanya menulis nama berkasnya, bukan path penuh.

Kalau proses mati karena `SIGSEGV`, `SIGBUS`, `SIGILL`, atau `SIGABRT`, log menulis satu blok
`ERROR CRASH signal=... pc=... module=... fault=... lr=...` berikut pemanggil `abort` dan
`__stack_chk_fail`, lalu beberapa frame yang alamatnya jatuh di dalam segmen kode modul. Blok
itu yang dipakai untuk mencari penyebab, bukan logcat.

## Catatan dan batasan

- Mod memeriksa nama berkas dan keutuhan isi berkas sekali di awal. Kalau salah satu tidak
  cocok, mod tidak memasang hook dan game berjalan seperti biasa. Tidak ada pemeriksaan proses
  atau library saat game berjalan, jadi sesi yang sudah aktif tidak bisa kehilangan hook di
  tengah permainan.
- Izin build dan destroy tetap ditentukan host room, jadi tamu tidak bisa mengubah tile di luar
  izin itu. Yang dikirim mod hanya paket kick lewat perintah chat, memakai kunci host
  room sebagai pengirim.
- Sushi, es krim, dan popsicle tetap habis seperti biasa, karena pose akhir makanannya mematikan
  layer tangan sehingga sisa item akan tidak terlihat. Makanan yang dimakan langsung dari piring
  tetap habis dalam satu gigitan seperti vanila.
- Skin tersimpan sebagai slot model game, yaitu
  `Android/data/<package>/files/Save/Model/<nomor>/Data.txt`, berurutan dari nomor kosong
  pertama. Isi tiap `Data.txt` sama dengan yang ditulis game saat menyimpan model sendiri. Slot
  yang sudah ada dibaca sekali di awal, jadi skin yang sudah pernah tersimpan tidak diduplikasi.
  Nama pemain asalnya ditulis ke `Name.txt` di folder slot yang sama begitu profil pemain itu
  diterima game; kalau profilnya tidak pernah datang, slot tetap tersimpan tanpa nama.
- Batas ukuran world memakai preset terbesar game, yaitu 600x300. World sebesar itu tetap butuh
  RAM; kalau perangkat kehabisan memori saat memuat world raksasa, game tetap bisa menutup
  dirinya sendiri.
- Isi pesan pribadi hanya versi teks; pesan suara pribadi tidak ditampilkan. Kalau pengirim
  sudah keluar room saat pesan masuk, namanya tidak bisa ditampilkan, jadi pesan itu hanya
  dicatat di log dan tidak muncul di chat room. Setiap DM yang tampil juga bisa memicu bunyi
  notifikasi chat.
- Sensor kata hanya dilewati di aplikasi yang memakai mod; klien tanpa mod tetap menyaring kata
  yang dikirim maupun diterimanya.
- Gunakan di akun sendiri; mod ini bisa melanggar ketentuan layanan game.
- Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
