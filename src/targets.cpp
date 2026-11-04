#include "targets.h"

#include <cstdint>
#include <cstring>

#include "il2cpp_api.h"

namespace {

Target targets[T_COUNT] = {};
char kolamNama[kTargetBlobSize + 1] = {};
bool siap = false;

uint64_t fnv1a64(const char* teks) {
    uint64_t nilai = 0xcbf29ce484222325ULL;
    for (const auto* p = reinterpret_cast<const unsigned char*>(teks); *p; p++)
        nilai = (nilai ^ *p) * 0x100000001b3ULL;
    return nilai;
}

uint64_t campur(uint64_t masukan) {
    masukan = (masukan ^ (masukan >> 30)) * 0xbf58476d1ce4e5b9ULL;
    masukan = (masukan ^ (masukan >> 27)) * 0x94d049bb133111ebULL;
    return masukan ^ (masukan >> 31);
}

uint8_t byteKunci(uint64_t hash, uint32_t indeks) {
    uint64_t nilai = kBuildSeed ^ hash ^ (static_cast<uint64_t>(indeks) * 0x9e3779b97f4a7c15ULL);
    return static_cast<uint8_t>(campur(nilai) & 0xFF);
}

// Overload bisa berbagi nama dan jumlah argumen, jadi sebuah target boleh juga
// menamai kelas parameter terakhirnya. Kelas array adalah "String[]" dan kelas
// generik mempertahankan nama "List`1", jadi perbandingan persis sudah cukup untuk
// memilih satu overload.
bool paramCocok(const Target& entri, void* metode) {
    if (!entri.paramType)
        return true;
    Api& api = apiIl2cpp();
    void* tipe = api.param(metode, static_cast<uint32_t>(entri.argc - 1));
    void* kelas = tipe ? api.classFromType(tipe) : nullptr;
    const char* nama = kelas ? api.className(kelas) : nullptr;
    return nama && !strcmp(entri.paramType, nama);
}

} // namespace

bool inisialisasiTarget(const char* namaPustaka) {
    if (siap)
        return true;
    if (!namaPustaka || !*namaPustaka)
        return false;

    uint64_t hash = fnv1a64(namaPustaka);
    if (hash != kExpectedLibraryHash)
        return false;

    for (uint32_t i = 0; i < kTargetBlobSize; i++)
        kolamNama[i] = static_cast<char>(kTargetBlob[i] ^ byteKunci(hash, i));
    kolamNama[kTargetBlobSize] = '\0';

    for (uint32_t i = 0; i < static_cast<uint32_t>(T_COUNT); i++) {
        const TargetData& data = kTargetData[i];
        Target& entri = targets[i];
        entri.cls = kolamNama + data.cls;
        entri.name = kolamNama + data.name;
        entri.paramType = data.param == kNoParam ? nullptr : kolamNama + data.param;
        entri.argc = data.argc;
    }
    siap = true;
    return true;
}

Target& target(TargetId id) { return targets[id]; }

int scan() {
    if (!siap)
        return 0;

    Api& api = apiIl2cpp();
    void* domain = api.domain();
    if (!domain)
        return 0;

    size_t jumlahAssembly = 0;
    void** assemblies = api.assemblies(domain, &jumlahAssembly);
    if (!assemblies)
        return 0;

    for (size_t i = 0; i < jumlahAssembly; i++) {
        void* image = api.image(assemblies[i]);
        if (!image)
            continue;

        size_t jumlahKelas = api.classCount(image);
        for (size_t j = 0; j < jumlahKelas; j++) {
            void* kelas = api.classAt(image, j);
            if (!kelas)
                continue;

            const char* namaKelas = api.className(kelas);
            if (!namaKelas)
                continue;

            bool dicari = false;
            for (const Target& entri : targets) {
                if (!strcmp(entri.cls, namaKelas)) {
                    dicari = true;
                    break;
                }
            }
            if (!dicari)
                continue;

            void* iterator = nullptr;
            while (void* metode = api.methods(kelas, &iterator)) {
                const char* namaMetode = api.methodName(metode);
                if (!namaMetode)
                    continue;

                int jumlahParam = api.paramCount(metode);
                for (Target& entri : targets) {
                    if (entri.addr)
                        continue;
                    if (strcmp(entri.cls, namaKelas) || strcmp(entri.name, namaMetode) ||
                        entri.argc != jumlahParam)
                        continue;
                    if (!paramCocok(entri, metode))
                        continue;

                    void* alamat = alamatMethod(metode);
                    if (!punyaBadan(alamat))
                        continue;
                    entri.addr = alamat;
                    entri.method = metode;
                    ikatKelas(namaKelas, kelas);
                }
            }
        }
    }

    int ditemukan = 0;
    for (const Target& entri : targets) {
        if (entri.addr)
            ditemukan++;
    }
    return ditemukan;
}
