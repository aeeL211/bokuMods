#pragma once

#include <cstddef>

#include "targets_generated.h"

struct Target {
    const char* cls;
    const char* name;
    const char* paramType;
    int argc;
    void* addr;
    void* method;
    void* orig;
    void* stub;
};

Target& target(TargetId id);

// Mendekripsi nama target dengan nama dasar pustaka yang benar-benar dimuat. Salinan
// yang diganti nama menurunkan kunci yang berbeda, jadi namanya keluar salah dan
// scan() tidak menemukan apa pun. Mengembalikan false bila nama dasar itu bukan yang
// dipakai saat tabel dibangun.
bool inisialisasiTarget(const char* namaPustaka);

// Mencari setiap target berdasarkan nama kelas, nama method, dan jumlah parameter.
// Mengembalikan berapa yang ditemukan, yang bernilai T_COUNT bila semuanya berjalan
// sehat.
int scan();
