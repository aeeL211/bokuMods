#pragma once

#include <cstddef>
#include <cstdint>

// Tabel fungsi IL2CPP diikat sekali oleh ikatApi(). Tabel itu diambil lewat
// sebuah fungsi pengambil supaya tidak ada tabel yang bisa berubah di lingkup
// berkas.
struct Api {
    void* (*domain)();
    void** (*assemblies)(void*, size_t*);
    void* (*image)(void*);
    size_t (*classCount)(void*);
    void* (*classAt)(void*, size_t);
    const char* (*className)(void*);
    void* (*methods)(void*, void**);
    const char* (*methodName)(void*);
    int (*paramCount)(void*);
    void* (*param)(void*, uint32_t);
    void* (*attach)(void*);
    void* (*fieldFromName)(void*, const char*);
    size_t (*fieldOffset)(void*);
    void* (*fieldType)(void*);
    void* (*classFromType)(void*);
    void* (*elementClass)(void*);
    void* (*staticFieldData)(void*);
    void (*fieldSetValue)(void*, void*, void*);
    uint32_t (*arrayLength)(void*);
    int32_t (*stringLength)(void*);
    uint16_t* (*stringChars)(void*);
    void* (*stringNewUtf16)(const uint16_t*, int32_t);
};

Api& apiIl2cpp();

bool ikatApi();

// Nama sederhana yang sama bisa ada di beberapa assembly, jadi sebuah kelas baru
// diterima setelah salah satu metodenya cocok dengan nama dan jumlah argumen
// yang diminta.
void* kelasDari(const char* nama);
void ikatKelas(const char* nama, void* kelas);

struct Field {
    void* field;
    size_t offset;
};

bool cariField(void* kelas, const char* nama, Field& hasil);
void* kelasField(const Field& bidang);
// Untuk field array, fungsi ini mengembalikan kelas elemennya, bukan kelas array-nya.
void* kelasElemenField(const Field& bidang);

void* bacaPtr(void* objek, const Field& bidang);
int32_t bacaInt(void* objek, const Field& bidang);
void tulisInt(void* objek, const Field& bidang, int32_t nilai);
float bacaFloat(void* objek, const Field& bidang);
void tulisFloat(void* objek, const Field& bidang, float nilai);
bool bacaBool(void* objek, const Field& bidang);
void tulisBool(void* objek, const Field& bidang, bool nilai);
void setelRef(void* objek, const Field& bidang, void* nilai);

int32_t panjangArray(void* array);
char* dataArray(void* array);
int32_t panjangString(void* teks);
const uint16_t* karakterString(void* teks);
// Dua string IL2CPP dibandingkan berdasarkan nilai; null di salah satu sisi
// tidak pernah dianggap sama.
bool samaString(void* pertama, void* kedua);
void* stringUtf16Baru(const uint16_t* karakter, int32_t panjang);
// Teks IL2CPP memakai UTF-16, sedangkan baris log dan path file memakai UTF-8.
bool keUtf8(void* teks, char* keluaran, size_t ukuran);
void* dataFieldStatis(void* kelas);

bool dekatNilai(float nilai, float target);

// MethodInfo runtime diawali oleh pointer kode hasil pencarian.
void* alamatMethod(void* metode);
// Method yang isinya hanya "return" tidak punya badan yang layak dipasangi hook.
bool punyaBadan(void* alamat);
