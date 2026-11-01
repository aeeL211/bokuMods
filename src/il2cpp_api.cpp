#include "il2cpp_api.h"

#include <dlfcn.h>

#include <cstring>

#include "log.h"

namespace {

Api api = {};

// The same simple name can exist in several assemblies ("Iap" and "_Unlock" are
// both duplicated), so every wanted class is listed here and only bound once the
// scan has matched a method of it.
struct ClassSlot {
    const char* name;
    void* klass;
};

ClassSlot classes[] = {
    {"Iap", nullptr},
    {"UI__Iap", nullptr},
    {"_Unlock", nullptr},
    {"Net__Multi__Client", nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List__Detail__Password", nullptr},
    {"UIInput", nullptr},
    {"UI__Manager", nullptr},
    {"Net__Account__Manager", nullptr},
    {"UI__Menu__Option__Profile", nullptr},
    {"MyBehaviour", nullptr},
    {"UI__Menu__Model__Main__Pos", nullptr},
    {"Net__Account__Portrait__Main__Pos", nullptr},
    {"UI__Menu__Option__About", nullptr},
    {"_Tile", nullptr},
};

// On arm64 the array header is klass, monitor and bounds (pointers) plus
// max_length (int32) and four bytes of padding, so data starts at 0x20.
constexpr size_t kArrayData = 4 * sizeof(void*);

} // namespace

Api& il2cppApi() { return api; }

void* classOf(const char* name) {
    for (ClassSlot& c : classes) {
        if (!strcmp(c.name, name))
            return c.klass;
    }
    return nullptr;
}

void bindClass(const char* name, void* klass) {
    for (ClassSlot& c : classes) {
        if (!c.klass && !strcmp(c.name, name))
            c.klass = klass;
    }
}

bool resolveField(void* klass, const char* name, Field& out) {
    if (!klass)
        return false;
    out.field = api.fieldFromName(klass, name);
    if (!out.field)
        return false;
    out.offset = api.fieldOffset(out.field);
    return true;
}

void* fieldClass(const Field& f) {
    if (!f.field || !api.fieldType || !api.classFromType)
        return nullptr;
    void* type = api.fieldType(f.field);
    return type ? api.classFromType(type) : nullptr;
}

void* fieldElementClass(const Field& f) {
    void* cls = fieldClass(f);
    if (!cls || !api.elementClass)
        return nullptr;
    return api.elementClass(cls);
}

void* readPtr(void* obj, const Field& f) {
    if (!obj || !f.field)
        return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + f.offset);
}

int32_t readInt(void* obj, const Field& f) {
    if (!obj || !f.field)
        return 0;
    return *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(obj) + f.offset);
}

void writeInt(void* obj, const Field& f, int32_t value) {
    if (!obj || !f.field)
        return;
    *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(obj) + f.offset) = value;
}

float readFloat(void* obj, const Field& f) {
    if (!obj || !f.field)
        return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<char*>(obj) + f.offset);
}

void writeFloat(void* obj, const Field& f, float value) {
    if (!obj || !f.field)
        return;
    *reinterpret_cast<float*>(reinterpret_cast<char*>(obj) + f.offset) = value;
}

void setRef(void* obj, const Field& f, void* value) {
    if (!obj || !f.field)
        return;
    if (api.fieldSetValue) {
        api.fieldSetValue(obj, f.field, &value);
    } else {
        *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + f.offset) = value;
    }
}

int32_t arrayLength(void* arr) {
    if (!arr || !api.arrayLength)
        return 0;
    return static_cast<int32_t>(api.arrayLength(arr));
}

char* arrayData(void* arr) { return reinterpret_cast<char*>(arr) + kArrayData; }

int32_t stringLength(void* str) {
    if (!str || !api.stringLength)
        return 0;
    return api.stringLength(str);
}

const uint16_t* stringChars(void* str) {
    if (!str || !api.stringChars)
        return nullptr;
    return api.stringChars(str);
}

void* newUtf16String(const uint16_t* chars, int32_t len) {
    if (!api.stringNewUtf16)
        return nullptr;
    return api.stringNewUtf16(chars, len);
}

void* staticFieldData(void* klass) {
    if (!klass || !api.staticFieldData)
        return nullptr;
    return api.staticFieldData(klass);
}

bool nearValue(float value, float target) {
    float d = value - target;
    return d > -0.01f && d < 0.01f;
}

void* methodAddr(void* m) { return m ? *reinterpret_cast<void**>(m) : nullptr; }

bool hasBody(void* a) {
    if (!a)
        return false;
    uintptr_t p = reinterpret_cast<uintptr_t>(a);
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(p), &info) || !info.dli_fbase)
        return false;
    return *reinterpret_cast<uint32_t*>(p) != 0xd65f03c0;
}

bool bindApi() {
    void* h = dlopen("libil2cpp.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!h)
        h = dlopen("libil2cpp.so", RTLD_LAZY);
    if (!h)
        return false;

#define BIND(f, sym)                                                                               \
    api.f = reinterpret_cast<decltype(api.f)>(dlsym(h, sym));                                      \
    if (!api.f) {                                                                                  \
        LOGE("Missing %s", sym);                                                                   \
        return false;                                                                              \
    }

    BIND(domain, "il2cpp_domain_get");
    BIND(assemblies, "il2cpp_domain_get_assemblies");
    BIND(image, "il2cpp_assembly_get_image");
    BIND(classCount, "il2cpp_image_get_class_count");
    BIND(classAt, "il2cpp_image_get_class");
    BIND(className, "il2cpp_class_get_name");
    BIND(methods, "il2cpp_class_get_methods");
    BIND(methodName, "il2cpp_method_get_name");
    BIND(paramCount, "il2cpp_method_get_param_count");
    BIND(attach, "il2cpp_thread_attach");
    BIND(fieldFromName, "il2cpp_class_get_field_from_name");
    BIND(fieldOffset, "il2cpp_field_get_offset");
    BIND(arrayLength, "il2cpp_array_length");
    BIND(stringLength, "il2cpp_string_length");
#undef BIND

    api.fieldType = reinterpret_cast<decltype(api.fieldType)>(dlsym(h, "il2cpp_field_get_type"));
    api.classFromType =
        reinterpret_cast<decltype(api.classFromType)>(dlsym(h, "il2cpp_class_from_il2cpp_type"));
    api.elementClass =
        reinterpret_cast<decltype(api.elementClass)>(dlsym(h, "il2cpp_class_get_element_class"));
    api.staticFieldData = reinterpret_cast<decltype(api.staticFieldData)>(
        dlsym(h, "il2cpp_class_get_static_field_data"));
    api.fieldSetValue =
        reinterpret_cast<decltype(api.fieldSetValue)>(dlsym(h, "il2cpp_field_set_value"));
    api.stringChars = reinterpret_cast<decltype(api.stringChars)>(dlsym(h, "il2cpp_string_chars"));
    api.stringNewUtf16 =
        reinterpret_cast<decltype(api.stringNewUtf16)>(dlsym(h, "il2cpp_string_new_utf16"));
    return true;
}
