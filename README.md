# Boku Mods

Mod native untuk **Boku Boku** (`com.pixticle.bokuboku`). Semua perubahan dilakukan lewat hook
pada method IL2CPP, jadi file game tidak diubah. Library-nya adalah `libfebrian.so` dan
ShadowHook sudah ikut di dalamnya.

## Fitur

| Fitur | Status | Catatan |
|---|---|---|
| Beli item/IAP gratis | Ya | Tanpa Play Billing |
| Unlock semua tile, model, dan melody | Ya | Ketiganya memakai gate yang sama |
| Nama profil panjang | Ya | Batas input dinaikkan |
| Tanggal lahir bebas | Ya | Hari dan tahun tidak dibatasi, bulan tetap 12 |
| Save world tanpa izin host | Ya | Build dan destroy tetap milik host |
| Join room ber-password tanpa mengisi | Ya | Tombol OK langsung aktif |
| Posisi X/Y skin -20..20 | Ya | Model dan portrait, sumbu rotate tidak diubah |
| Makan gingerbread tanpa batas | Ya | Kue tetap di tangan setelah 3 gigitan |
| Label versi "(modded)" | Ya | About, layar judul, dan bantuan multiplayer |
| Toast saat mod aktif | Ya | `MODS: Logged in as <nama>` |
| Join room yang penuh | Tidak | Dibatasi host dan Photon |

## Pemasangan

1. Siapkan `libfebrian.so` hasil build.
2. Letakkan di folder library proses game, lalu muat lewat loader/injector.
3. Buka game. Toast `MODS: Logged in` muncul kalau mod sudah aktif.

Tidak ada file tambahan yang perlu disalin: ShadowHook sudah statis di dalam `libfebrian.so`.

## Toast

- `MODS: Logged in as <nama>` saat mod aktif. Nama dibaca dari
  `Android/data/<package>.mods/files/Setting/User_Name.txt`, dengan `<package>` diambil dari
  proses game. Nama lebih dari 64 karakter dipotong pada batas karakter lalu diberi `"..."`.
  Kalau file tidak ada, kosong, atau tidak bisa dibaca, toast menjadi `MODS: Logged in` dan
  alasannya dicatat di log.
- `MODS: Purchase simulated successfully` setiap kali pembelian dialihkan tanpa Play Billing.

## Batasan

- **Join room penuh.** Kapasitas dicek oleh host dan ditolak Photon, jadi tidak bisa diakali
  dari klien.
- **Build dan destroy lintas host.** Izin tetap milik host; kalau setting room melarang, aksi
  tetap ditolak.
- **Validasi sisi server.** Batas nama dan tanggal lahir masih bisa diperiksa ulang oleh server
  atau Firebase.
- **Room besar di perangkat RAM kecil.** Batas ukuran room berjalan di sisi host dan tidak bisa
  dinaikkan dari klien.

## Log

Log hanya ditulis ke file, bukan logcat, di
`Android/data/com.pixticle.bokuboku/files/bokumods.log`. File yang sudah ada akan ditambahi,
jadi semua sesi ada di satu file:

```
[15:14:12] INFO  Log start (2026-11-01)
[15:14:12] INFO  Log file: /storage/emulated/0/Android/data/com.pixticle.bokuboku/files/bokumods.log (append)
[15:14:12] INFO  Log kept: 48120 bytes from earlier runs
[15:14:13] INFO  ShadowHook initialized
[15:14:15] INFO  Scan done: found 29 of 29 methods
[15:14:15] INFO  Hooked UIInput.Insert at 0x... (a64)
```

Kalau file lama tidak bisa ditambahi, isinya disalin ke file baru di path yang sama. Kalau isinya
tidak bisa dibaca, file lama dipindah ke `bokumods.log.old` dan log mulai lagi dari awal, supaya
nama file tetap satu. Baris yang muncul sebelum folder log siap disimpan di memori lalu ditulis
saat file berhasil dibuka.

Kalau proses mati karena fault, handler sinyal menulis baris `ERROR CRASH` berisi sinyal dan
alamat PC ke file yang sama. Baris `Target not found` atau `Hook failed` berarti fitur terkait
tidak aktif, biasanya karena nama class/method berubah di versi game lain.

## Catatan

- Hanya untuk Boku Boku; nama class/method bisa berubah di versi game lain.
- Hanya mendukung `arm64-v8a`. Perangkat 32-bit tidak didukung.
- Gunakan di perangkat dan akun sendiri. Mod ini dapat melanggar ketentuan layanan game.
- Tidak ada jaminan; gunakan dengan risiko sendiri.
- Lisensi pihak ketiga ada di `THIRD_PARTY_NOTICES.md`.
- Terima kasih kepada ShadowHook (ByteDance) dan API IL2CPP.
