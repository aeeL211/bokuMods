#include "protect.h"

#include <dlfcn.h>
#include <elf.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "targets.h"

namespace {

// Langkah build menulis hash setiap segmen PT_LOAD yang tidak bisa ditulis setelah
// penanda ini. Penandanya sendiri berada di segmen data yang bisa ditulis, jadi
// penulisan hash tidak mengubah nilai yang dijelaskannya.
struct BlokIntegritas {
    char penanda[16];
    uint64_t hash;
};
volatile BlokIntegritas blokIntegritas = {"BOKUINTEGRITYv1", 0};

void* dasarPustaka = nullptr;

const char* namaDasar(const char* path) {
    const char* garisMiring = strrchr(path, '/');
    return garisMiring ? garisMiring + 1 : path;
}

bool infoPustaka(char* path, size_t ukuran) {
    Dl_info informasi;
    if (!dladdr(reinterpret_cast<void*>(&infoPustaka), &informasi) || !informasi.dli_fbase ||
        !informasi.dli_fname)
        return false;
    dasarPustaka = informasi.dli_fbase;
    snprintf(path, ukuran, "%s", informasi.dli_fname);
    char* terhapus = strstr(path, " (deleted)");
    if (terhapus)
        *terhapus = '\0';
    return true;
}

bool integritasCocok() {
    if (!dasarPustaka)
        return false;
    const auto* kepalaElf = static_cast<const Elf64_Ehdr*>(dasarPustaka);
    if (memcmp(kepalaElf->e_ident, ELFMAG, SELFMAG) != 0 ||
        kepalaElf->e_ident[EI_CLASS] != ELFCLASS64 ||
        kepalaElf->e_ident[EI_DATA] != ELFDATA2LSB || kepalaElf->e_machine != EM_AARCH64)
        return false;

    // Kepala ELF selalu ada di halaman pertama, jadi tabel program header di luarnya
    // menunjuk memori yang tidak pernah dipetakan proses ini. Pustaka yang rusak
    // atau terpotong harus gagal tertutup daripada berjalan keluar dari image.
    constexpr Elf64_Off kBatasOffsetPhdr = 4096;
    constexpr Elf64_Half kBatasJumlahPh = 128;
    if (kepalaElf->e_phoff == 0 || kepalaElf->e_phoff > kBatasOffsetPhdr ||
        kepalaElf->e_phnum == 0 || kepalaElf->e_phnum > kBatasJumlahPh ||
        kepalaElf->e_phentsize != sizeof(Elf64_Phdr))
        return false;
    if (kepalaElf->e_phoff + static_cast<Elf64_Off>(kepalaElf->e_phnum) * sizeof(Elf64_Phdr) >
        kBatasOffsetPhdr)
        return false;

    const auto* segmen = reinterpret_cast<const Elf64_Phdr*>(
        static_cast<const uint8_t*>(dasarPustaka) + kepalaElf->e_phoff);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < kepalaElf->e_phnum; i++) {
        if (segmen[i].p_type != PT_LOAD || (segmen[i].p_flags & PF_W))
            continue;
        // Rentang file yang lebih besar dari rentang memorinya, atau yang membungkus
        // ruang alamat, tidak bisa menjelaskan segmen yang dipetakan proses ini.
        if (segmen[i].p_filesz > segmen[i].p_memsz ||
            segmen[i].p_vaddr > UINT64_MAX - segmen[i].p_filesz)
            return false;
        const auto* awal = static_cast<const uint8_t*>(dasarPustaka) + segmen[i].p_vaddr;
        for (Elf64_Xword j = 0; j < segmen[i].p_filesz; j++)
            hash = (hash ^ awal[j]) * 0x100000001b3ULL;
    }

    uint64_t tersimpan = blokIntegritas.hash;
    return tersimpan != 0 && hash == tersimpan;
}

} // namespace

bool inisialisasiProteksi() {
    char pathPustaka[1024] = {};
    if (!infoPustaka(pathPustaka, sizeof(pathPustaka))) {
        LOGE("Protection: path pustaka tidak tersedia");
        return false;
    }
    if (!inisialisasiTarget(namaDasar(pathPustaka))) {
        LOGE("Protection: nama pustaka tidak cocok");
        return false;
    }
    if (!integritasCocok()) {
        LOGE("Protection: pemeriksaan integritas kode gagal");
        return false;
    }
    return true;
}
