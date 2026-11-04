#include "il2cpp_api.h"

#include <dlfcn.h>

#include <cstring>

#include "log.h"

namespace {

Api api = {};

// Nama sederhana yang sama bisa ada di beberapa assembly ("Iap" dan "_Unlock"
// sama-sama terduplikasi), jadi setiap kelas yang dibutuhkan didaftarkan di sini
// dan baru diikat setelah scan mencocokkan salah satu metodenya.
struct SlotKelas {
    const char* nama;
    void* kelas;
};

SlotKelas daftarKelas[] = {
    {"Iap", nullptr},
    {"UI__Iap", nullptr},
    {"_Unlock", nullptr},
    {"Net__Multi__Client", nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List__Detail__Password", nullptr},
    {"UIInput", nullptr},
    {"UI__Manager", nullptr},
    {"Net__Account__Manager", nullptr},
    {"Net__Account__My_Account__Detail", nullptr},
    {"Net__Account__Verify__Detail", nullptr},
    {"Net__Account__Friend_Profile", nullptr},
    {"Net__Account__Add_Friend", nullptr},
    {"UI__Menu__Option__Profile", nullptr},
    {"MyBehaviour", nullptr},
    {"UI__Menu__Model__Main__Pos", nullptr},
    {"Net__Account__Portrait__Main__Pos", nullptr},
    {"UI__Menu__Option__About", nullptr},
    {"Title__Cmn", nullptr},
    {"UI__Menu__Net__Multi__Help", nullptr},
    {"_Tile", nullptr},
    {"Global", nullptr},
    {"Net__Multi__Transfer__Pipe", nullptr},
    {"Net__Multi__Transfer__Host", nullptr},
    {"Net__Multi__Chat", nullptr},
    {"Net__Multi", nullptr},
    {"Net__Multi__Guest__Chat", nullptr},
    {"Guest__Manager", nullptr},
    {"UI__Menu__World__New__Setting__Size", nullptr},
    {"Player__State", nullptr},
    {"Player__State__Cmn", nullptr},
    {"Player__State__Std__Action__Hh__Weapon", nullptr},
    {"Camera__Manager", nullptr},
    {"Eff__Type__Tile", nullptr},
    {"Player__Manager", nullptr},
    {"Player__State__Std__Action__Hh__Nature", nullptr},
};

// Di arm64, kepala array berisi klass, monitor, dan bounds (pointer) ditambah
// max_length (int32) dan empat byte padding, jadi data dimulai di 0x20.
constexpr size_t kDataArray = 4 * sizeof(void*);

} // namespace

Api& apiIl2cpp() { return api; }

void* kelasDari(const char* nama) {
    for (SlotKelas& s : daftarKelas) {
        if (!strcmp(s.nama, nama))
            return s.kelas;
    }
    return nullptr;
}

void ikatKelas(const char* nama, void* kelas) {
    for (SlotKelas& s : daftarKelas) {
        if (!s.kelas && !strcmp(s.nama, nama))
            s.kelas = kelas;
    }
}

bool cariField(void* kelas, const char* nama, Field& hasil) {
    if (!kelas)
        return false;
    hasil.field = api.fieldFromName(kelas, nama);
    if (!hasil.field)
        return false;
    hasil.offset = api.fieldOffset(hasil.field);
    return true;
}

void* kelasField(const Field& bidang) {
    if (!bidang.field || !api.fieldType || !api.classFromType)
        return nullptr;
    void* tipe = api.fieldType(bidang.field);
    return tipe ? api.classFromType(tipe) : nullptr;
}

void* kelasElemenField(const Field& bidang) {
    void* kelas = kelasField(bidang);
    if (!kelas || !api.elementClass)
        return nullptr;
    return api.elementClass(kelas);
}

void* bacaPtr(void* objek, const Field& bidang) {
    if (!objek || !bidang.field)
        return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(objek) + bidang.offset);
}

int32_t bacaInt(void* objek, const Field& bidang) {
    if (!objek || !bidang.field)
        return 0;
    return *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(objek) + bidang.offset);
}

void tulisInt(void* objek, const Field& bidang, int32_t nilai) {
    if (!objek || !bidang.field)
        return;
    *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(objek) + bidang.offset) = nilai;
}

float bacaFloat(void* objek, const Field& bidang) {
    if (!objek || !bidang.field)
        return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<char*>(objek) + bidang.offset);
}

void tulisFloat(void* objek, const Field& bidang, float nilai) {
    if (!objek || !bidang.field)
        return;
    *reinterpret_cast<float*>(reinterpret_cast<char*>(objek) + bidang.offset) = nilai;
}

bool bacaBool(void* objek, const Field& bidang) {
    if (!objek || !bidang.field)
        return false;
    return *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(objek) + bidang.offset) != 0;
}

void tulisBool(void* objek, const Field& bidang, bool nilai) {
    if (!objek || !bidang.field)
        return;
    *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(objek) + bidang.offset) =
        nilai ? 1 : 0;
}

// il2cpp_field_set_value pada build ini menyimpan referensinya langsung: fungsi
// penyetelnya menerima pointer objek dan tidak pernah membacanya. Memberi pointer
// ke variabel lokal menaruh alamat stack di field, lalu pengumpul sampah membebaskan
// string yang seharusnya dijaga hidup oleh field itu. Pembacaan ulang menutup build
// yang justru mengharapkan pointer ke nilai.
void setelRef(void* objek, const Field& bidang, void* nilai) {
    if (!objek || !bidang.field)
        return;
    if (api.fieldSetValue) {
        api.fieldSetValue(objek, bidang.field, nilai);
        if (bacaPtr(objek, bidang) == nilai)
            return;
        api.fieldSetValue(objek, bidang.field, &nilai);
        if (bacaPtr(objek, bidang) == nilai)
            return;
    }
    *reinterpret_cast<void**>(reinterpret_cast<char*>(objek) + bidang.offset) = nilai;
}

int32_t panjangArray(void* array) {
    if (!array || !api.arrayLength)
        return 0;
    return static_cast<int32_t>(api.arrayLength(array));
}

char* dataArray(void* array) { return reinterpret_cast<char*>(array) + kDataArray; }

int32_t panjangString(void* teks) {
    if (!teks || !api.stringLength)
        return 0;
    return api.stringLength(teks);
}

const uint16_t* karakterString(void* teks) {
    if (!teks || !api.stringChars)
        return nullptr;
    return api.stringChars(teks);
}

bool samaString(void* pertama, void* kedua) {
    if (!pertama || !kedua)
        return false;
    if (pertama == kedua)
        return true;
    int32_t panjang = panjangString(pertama);
    if (panjang != panjangString(kedua))
        return false;
    const uint16_t* karakterPertama = karakterString(pertama);
    const uint16_t* karakterKedua = karakterString(kedua);
    if (!karakterPertama || !karakterKedua)
        return false;
    for (int32_t i = 0; i < panjang; i++) {
        if (karakterPertama[i] != karakterKedua[i])
            return false;
    }
    return true;
}

void* stringUtf16Baru(const uint16_t* karakter, int32_t panjang) {
    if (!api.stringNewUtf16)
        return nullptr;
    return api.stringNewUtf16(karakter, panjang);
}

namespace {

// Paling banyak empat byte, jadi pemanggil yang menyisakan empat byte punya ruang
// untuk kode apa pun.
size_t tulisTitikKode(uint32_t kode, char* keluaran) {
    if (kode < 0x80) {
        keluaran[0] = static_cast<char>(kode);
        return 1;
    }
    if (kode < 0x800) {
        keluaran[0] = static_cast<char>(0xC0 | (kode >> 6));
        keluaran[1] = static_cast<char>(0x80 | (kode & 0x3F));
        return 2;
    }
    if (kode < 0x10000) {
        keluaran[0] = static_cast<char>(0xE0 | (kode >> 12));
        keluaran[1] = static_cast<char>(0x80 | ((kode >> 6) & 0x3F));
        keluaran[2] = static_cast<char>(0x80 | (kode & 0x3F));
        return 3;
    }
    keluaran[0] = static_cast<char>(0xF0 | (kode >> 18));
    keluaran[1] = static_cast<char>(0x80 | ((kode >> 12) & 0x3F));
    keluaran[2] = static_cast<char>(0x80 | ((kode >> 6) & 0x3F));
    keluaran[3] = static_cast<char>(0x80 | (kode & 0x3F));
    return 4;
}

} // namespace

bool keUtf8(void* teks, char* keluaran, size_t ukuran) {
    int32_t panjang = panjangString(teks);
    const uint16_t* karakter = karakterString(teks);
    if (panjang <= 0 || !karakter)
        return false;

    size_t terpakai = 0;
    for (int32_t i = 0; i < panjang; i++) {
        uint32_t kode = karakter[i];
        if (kode >= 0xD800 && kode <= 0xDBFF && i + 1 < panjang) {
            uint32_t rendah = karakter[i + 1];
            if (rendah >= 0xDC00 && rendah <= 0xDFFF) {
                kode = 0x10000 + ((kode - 0xD800) << 10) + (rendah - 0xDC00);
                i++;
            }
        }
        if (kode >= 0xD800 && kode <= 0xDFFF)
            kode = 0xFFFD;
        if (terpakai + 4 >= ukuran)
            return false;
        terpakai += tulisTitikKode(kode, keluaran + terpakai);
    }
    keluaran[terpakai] = '\0';
    return true;
}

void* dataFieldStatis(void* kelas) {
    if (!kelas || !api.staticFieldData)
        return nullptr;
    return api.staticFieldData(kelas);
}

bool dekatNilai(float nilai, float target) {
    float selisih = nilai - target;
    return selisih > -0.01f && selisih < 0.01f;
}

void* alamatMethod(void* metode) { return metode ? *reinterpret_cast<void**>(metode) : nullptr; }

bool punyaBadan(void* alamat) {
    if (!alamat)
        return false;
    uintptr_t posisi = reinterpret_cast<uintptr_t>(alamat);
    Dl_info informasi;
    if (!dladdr(reinterpret_cast<void*>(posisi), &informasi) || !informasi.dli_fbase)
        return false;
    return *reinterpret_cast<uint32_t*>(posisi) != 0xd65f03c0;
}

bool ikatApi() {
    void* pustaka = dlopen("libil2cpp.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!pustaka)
        pustaka = dlopen("libil2cpp.so", RTLD_LAZY);
    if (!pustaka)
        return false;

#define IKAT(anggota, simbol)                                                                      \
    api.anggota = reinterpret_cast<decltype(api.anggota)>(dlsym(pustaka, simbol));                 \
    if (!api.anggota) {                                                                            \
        LOGE("Simbol tidak ditemukan: %s", simbol);                                                \
        return false;                                                                              \
    }

    IKAT(domain, "il2cpp_domain_get");
    IKAT(assemblies, "il2cpp_domain_get_assemblies");
    IKAT(image, "il2cpp_assembly_get_image");
    IKAT(classCount, "il2cpp_image_get_class_count");
    IKAT(classAt, "il2cpp_image_get_class");
    IKAT(className, "il2cpp_class_get_name");
    IKAT(methods, "il2cpp_class_get_methods");
    IKAT(methodName, "il2cpp_method_get_name");
    IKAT(paramCount, "il2cpp_method_get_param_count");
    IKAT(param, "il2cpp_method_get_param");
    IKAT(attach, "il2cpp_thread_attach");
    IKAT(fieldFromName, "il2cpp_class_get_field_from_name");
    IKAT(fieldOffset, "il2cpp_field_get_offset");
    IKAT(arrayLength, "il2cpp_array_length");
    IKAT(stringLength, "il2cpp_string_length");
#undef IKAT

    api.fieldType =
        reinterpret_cast<decltype(api.fieldType)>(dlsym(pustaka, "il2cpp_field_get_type"));
    api.classFromType = reinterpret_cast<decltype(api.classFromType)>(
        dlsym(pustaka, "il2cpp_class_from_il2cpp_type"));
    api.elementClass = reinterpret_cast<decltype(api.elementClass)>(
        dlsym(pustaka, "il2cpp_class_get_element_class"));
    api.staticFieldData = reinterpret_cast<decltype(api.staticFieldData)>(
        dlsym(pustaka, "il2cpp_class_get_static_field_data"));
    api.fieldSetValue =
        reinterpret_cast<decltype(api.fieldSetValue)>(dlsym(pustaka, "il2cpp_field_set_value"));
    api.stringChars =
        reinterpret_cast<decltype(api.stringChars)>(dlsym(pustaka, "il2cpp_string_chars"));
    api.stringNewUtf16 =
        reinterpret_cast<decltype(api.stringNewUtf16)>(dlsym(pustaka, "il2cpp_string_new_utf16"));
    return true;
}
