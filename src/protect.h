#pragma once

// Memeriksa nama pustaka, mendekripsi tabel target, dan memverifikasi hash
// integritas build. Mengembalikan false bila lingkungan tidak cocok dengan build,
// dan dalam hal itu pemanggil harus membiarkan game tidak tersentuh.
bool inisialisasiProteksi();
