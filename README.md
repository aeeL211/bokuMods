# febrian

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Library ini berjalan di dalam proses
game dan mengubah perilaku beberapa fitur lewat hook pada method IL2CPP.

Repo ini hanya berisi library native. Loader/injector dan APK yang sudah dipatch tidak termasuk,
jadi library ini harus dimuat sendiri ke dalam proses game.

ShadowHook tidak lagi ikut sebagai file terpisah: sumbernya diambil saat build dan ditautkan
statis ke dalam `libfebrian.so`.

## Fitur

| Fitur | Status | Catatan |
|---|---|---|
| Beli item/IAP gratis | Ya | Tanpa Play Billing; muncul toast `MODS: Purchase simulated successfully` |
| Unlock semua tile, model, dan melody | Ya | Ketiganya memakai gate yang sama |
| Nama profil panjang | Ya | Batas nama dinaikkan, bukan hanya batas field |
| Hari/tahun lahir tanpa batas | Ya | Hari 31 dan tahun 9999 terbuka; bulan tetap 12 |
| Save world tanpa izin host | Ya | Hanya save-world; build/destroy tetap milik host |
| Join room ber-password tanpa mengetik | Ya | Kotak password tetap kosong, tombol OK tetap aktif |
| Posisi X/Y skin -20..20 | Ya | Berlaku di model dan portrait; sumbu rotate tidak diubah |
| Makan gingerbread tanpa batas | Ya | Kue tidak hilang setelah dimakan; berlaku untuk semua varian gingerbread |
| Versi game berlabel "(modded)" | Ya | Teks versi asli di halaman About ditambah akhiran |
| Toast saat mod aktif | Ya | `MODS: Logged in as <nama>`; nama diambil dari `User_Name.txt` |
| Join room yang penuh | Tidak | Dibatasi host dan Photon |

## Pemasangan

1. Siapkan satu file: `libfebrian.so`.
2. Letakkan file itu di folder library yang sama di dalam proses game.
3. Muat `libfebrian.so` dari loader/injector, atau lewat `System.loadLibrary("febrian")` pada
   `UnityPlayerActivity` yang sudah dipatch.
4. Buka game. Toast konfirmasi muncul saat mod aktif.

Tidak ada lagi `libshadowhook.so` atau `libshadowhook_nothing.so` yang perlu disalin. Kalau hasil
build juga membutuhkan `libc++_shared.so`, sertakan library tersebut.

## Toast

Mod menampilkan dua pesan:

- `MODS: Logged in as <nama>` saat mod aktif. Nama dibaca dari
  `Android/data/com.pixticle.bokuboku.mods/files/Setting/User_Name.txt` sebagai UTF-8, spasi di
  awal dan akhir dibuang, dan nama lebih dari 20 karakter dipotong pada batas karakter lalu
  diberi `"..."`. Kalau file tidak ada, kosong, tidak bisa dibaca, atau bukan UTF-8 yang valid,
  toast menjadi `MODS: Logged in` dan alasannya dicatat di log.
- `MODS: Purchase simulated successfully` setiap kali pembelian dialihkan tanpa Play Billing.

## Yang tidak bisa

**Join room penuh.** Kapasitas dicek di dua tempat yang tidak bisa diakali dari klien: host
membandingkan jumlah pemain dengan batas room, dan Photon sendiri menolak join kalau
`MaxPlayers` sudah tercapai.

**Build dan destroy lintas host.** Izin build/destroy tetap milik host. Kalau host tidak memakai
mod dan setting room melarangnya, aksi tetap ditolak walaupun klien memaksa tombolnya aktif.

**Room besar di perangkat RAM kecil.** Batas ukuran room dan pemeriksaan memori berjalan di sisi
host. Klien tidak bisa menaikkannya, dan room yang sangat besar tetap bisa membuat perangkat
dengan RAM kecil lag atau crash.

**Validasi sisi server.** Batas nama dan isi tanggal lahir masih bisa diperiksa ulang oleh server
atau Firebase. Menghapus batas di klien tidak menjamin server menerima nilai yang sama. Nama
yang terlalu panjang juga bisa ditolak saat login atau sinkronisasi akun.

## Log

Log hanya ditulis ke file, tidak ke logcat, di
`Android/data/com.pixticle.bokuboku/files/bokumods.log`. Contoh isi:

```
[15:14:12] INFO  Log start (2026-11-01)
[15:14:12] INFO  Log file: /storage/emulated/0/Android/data/com.pixticle.bokuboku/files/bokumods.log (append)
[15:14:12] INFO  Library loaded
[15:14:13] INFO  ShadowHook initialized
[15:14:15] INFO  Scan done: found 22 of 22 methods
[15:14:15] INFO  Gingerbread ids: single=1234 plural=1235
[15:14:15] INFO  Hooked UIInput.Insert at 0x... (a64)
[15:14:15] INFO  Hooked Converter__Tile__Type.Eat__Tile__Change__Add at 0x... (a64)
```

File lama tidak dihapus: file dibuka dengan mode append, jadi tiap sesi menambah baris di
bawahnya dan diawali baris `Log start`. Kalau file lama tidak bisa ditambah, mod mencoba
memperbaiki permission, lalu memakai `bokumods-2.log` sebagai nama cadangan dan mencatat
alasannya. Baris yang muncul sebelum folder log siap disimpan dulu di memori lalu ditulis saat
file berhasil dibuka, jadi baris awal tidak hilang.

Kalau proses mati karena fault, handler sinyal menulis baris `ERROR CRASH` berisi sinyal,
alamat PC, dan register ke file yang sama. Baris `Target not found` atau `Hook failed` berarti
fitur terkait tidak aktif, biasanya karena nama class/method berubah di versi game tersebut.
Baris `Tile table not found` berarti hook gingerbread dimatikan dan kue kembali hilang seperti
biasa.

## Catatan

- Hanya untuk Boku Boku; nama class/method bisa berubah di versi game lain.
- Hanya mendukung `arm64-v8a`. Perangkat 32-bit tidak didukung.
- Gunakan di perangkat dan akun sendiri. Mod ini dapat melanggar ketentuan layanan game.
- Tidak ada jaminan; gunakan dengan risiko sendiri.
- Kode mod ini belum punya lisensi. Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
- Terima kasih kepada ShadowHook (ByteDance) dan API IL2CPP.
