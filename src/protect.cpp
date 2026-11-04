#include "protect.h"

#include <dlfcn.h>
#include <elf.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "targets.h"

namespace {

// The build step writes the hash of every non-writable PT_LOAD segment after
// this marker. The marker itself lives in the writable data segment, so writing
// the hash does not change the value it describes.
struct IntegrityBlock {
    char marker[16];
    uint64_t hash;
};
volatile IntegrityBlock integrityBlock = {"BOKUINTEGRITYv1", 0};

void* libraryBase = nullptr;

const char* baseName(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool libraryInfo(char* path, size_t size) {
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&libraryInfo), &info) || !info.dli_fbase || !info.dli_fname)
        return false;
    libraryBase = info.dli_fbase;
    snprintf(path, size, "%s", info.dli_fname);
    char* deleted = strstr(path, " (deleted)");
    if (deleted)
        *deleted = '\0';
    return true;
}

bool integrityOk() {
    if (!libraryBase)
        return false;
    const auto* eh = static_cast<const Elf64_Ehdr*>(libraryBase);
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB || eh->e_machine != EM_AARCH64)
        return false;

    // The header itself is always in the first page, so a program header table
    // beyond it points at memory this process never mapped. A malformed or
    // truncated library must fail closed instead of walking off the image.
    constexpr Elf64_Off kMaxPhdrOffset = 4096;
    constexpr Elf64_Half kMaxPhnum = 128;
    if (eh->e_phoff == 0 || eh->e_phoff > kMaxPhdrOffset || eh->e_phnum == 0 ||
        eh->e_phnum > kMaxPhnum || eh->e_phentsize != sizeof(Elf64_Phdr))
        return false;
    if (eh->e_phoff + static_cast<Elf64_Off>(eh->e_phnum) * sizeof(Elf64_Phdr) > kMaxPhdrOffset)
        return false;

    const auto* ph =
        reinterpret_cast<const Elf64_Phdr*>(static_cast<const uint8_t*>(libraryBase) + eh->e_phoff);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || (ph[i].p_flags & PF_W))
            continue;
        // A file range larger than its memory range, or one that wraps the
        // address space, cannot describe a segment this process mapped.
        if (ph[i].p_filesz > ph[i].p_memsz || ph[i].p_vaddr > UINT64_MAX - ph[i].p_filesz)
            return false;
        const auto* start = static_cast<const uint8_t*>(libraryBase) + ph[i].p_vaddr;
        for (Elf64_Xword j = 0; j < ph[i].p_filesz; j++)
            hash = (hash ^ start[j]) * 0x100000001b3ULL;
    }

    uint64_t expected = integrityBlock.hash;
    return expected != 0 && hash == expected;
}

} // namespace

bool initProtection() {
    char libraryPath[1024] = {};
    if (!libraryInfo(libraryPath, sizeof(libraryPath))) {
        LOGE("Protection: library path unavailable");
        return false;
    }
    if (!initTargets(baseName(libraryPath))) {
        LOGE("Protection: library name mismatch");
        return false;
    }
    if (!integrityOk()) {
        LOGE("Protection: code integrity check failed");
        return false;
    }
    return true;
}
