#include "targets.h"

#include <cstdint>
#include <cstring>

#include "il2cpp_api.h"

namespace {

Target targets[T_COUNT] = {};
char namePool[kTargetBlobSize + 1] = {};
bool initialized = false;

uint64_t fnv1a64(const char* text) {
    uint64_t value = 0xcbf29ce484222325ULL;
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p; p++)
        value = (value ^ *p) * 0x100000001b3ULL;
    return value;
}

uint64_t mix(uint64_t value) {
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint8_t keyByte(uint64_t hash, uint32_t index) {
    uint64_t value = kBuildSeed ^ hash ^ (static_cast<uint64_t>(index) * 0x9e3779b97f4a7c15ULL);
    return static_cast<uint8_t>(mix(value) & 0xFF);
}

// Overloads can share a name and an arity, so a target may also name the class
// of its last parameter. An array class is "String[]" and a generic one keeps
// its "List`1" name, so an exact compare is enough to pick one overload.
bool paramMatches(const Target& t, void* m) {
    if (!t.paramType)
        return true;
    Api& api = il2cppApi();
    void* type = api.param(m, static_cast<uint32_t>(t.argc - 1));
    void* cls = type ? api.classFromType(type) : nullptr;
    const char* name = cls ? api.className(cls) : nullptr;
    return name && !strcmp(t.paramType, name);
}

} // namespace

bool initTargets(const char* libraryName) {
    if (initialized)
        return true;
    if (!libraryName || !*libraryName)
        return false;

    uint64_t hash = fnv1a64(libraryName);
    if (hash != kExpectedLibraryHash)
        return false;

    for (uint32_t i = 0; i < kTargetBlobSize; i++)
        namePool[i] = static_cast<char>(kTargetBlob[i] ^ keyByte(hash, i));
    namePool[kTargetBlobSize] = '\0';

    for (uint32_t i = 0; i < static_cast<uint32_t>(T_COUNT); i++) {
        const TargetData& data = kTargetData[i];
        Target& t = targets[i];
        t.cls = namePool + data.cls;
        t.name = namePool + data.name;
        t.paramType = data.param == kNoParam ? nullptr : namePool + data.param;
        t.argc = data.argc;
    }
    initialized = true;
    return true;
}

Target& target(TargetId id) { return targets[id]; }

int scan() {
    if (!initialized)
        return 0;

    Api& api = il2cppApi();
    void* dom = api.domain();
    if (!dom)
        return 0;

    size_t assemblyCount = 0;
    void** assemblies = api.assemblies(dom, &assemblyCount);
    if (!assemblies)
        return 0;

    for (size_t i = 0; i < assemblyCount; i++) {
        void* img = api.image(assemblies[i]);
        if (!img)
            continue;

        size_t count = api.classCount(img);
        for (size_t j = 0; j < count; j++) {
            void* cls = api.classAt(img, j);
            if (!cls)
                continue;

            const char* cn = api.className(cls);
            if (!cn)
                continue;

            bool wanted = false;
            for (const Target& t : targets) {
                if (!strcmp(t.cls, cn)) {
                    wanted = true;
                    break;
                }
            }
            if (!wanted)
                continue;

            void* it = nullptr;
            while (void* m = api.methods(cls, &it)) {
                const char* mn = api.methodName(m);
                if (!mn)
                    continue;

                int argc = api.paramCount(m);
                for (Target& t : targets) {
                    if (t.addr)
                        continue;
                    if (strcmp(t.cls, cn) || strcmp(t.name, mn) || t.argc != argc)
                        continue;
                    if (!paramMatches(t, m))
                        continue;

                    void* a = methodAddr(m);
                    if (!hasBody(a))
                        continue;
                    t.addr = a;
                    t.method = m;
                    bindClass(cn, cls);
                }
            }
        }
    }

    int found = 0;
    for (const Target& t : targets) {
        if (t.addr)
            found++;
    }
    return found;
}
