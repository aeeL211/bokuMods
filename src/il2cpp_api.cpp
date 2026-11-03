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
    {"Net__Multi__Guest__Chat", nullptr},
    {"Guest__Manager", nullptr},
    {"UI__Menu__World__New__Setting__Size", nullptr},
    {"Player__State", nullptr},
    {"Player__State__Cmn", nullptr},
    {"Player__State__Std__Action__Hh__Weapon", nullptr},
    {"Camera__Manager", nullptr},
    {"Eff__Type__Tile", nullptr},
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

bool readBool(void* obj, const Field& f) {
    if (!obj || !f.field)
        return false;
    return *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(obj) + f.offset) != 0;
}

void writeBool(void* obj, const Field& f, bool value) {
    if (!obj || !f.field)
        return;
    *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(obj) + f.offset) = value ? 1 : 0;
}

// il2cpp_field_set_value in this build stores the reference itself: its setter
// takes the object pointer and never reads through it. Passing a pointer to the
// local variable put a stack address in the field, and the collector then freed
// the string that the field was supposed to keep alive. The read-back covers a
// build that instead expects a pointer to the value.
void setRef(void* obj, const Field& f, void* value) {
    if (!obj || !f.field)
        return;
    if (api.fieldSetValue) {
        api.fieldSetValue(obj, f.field, value);
        if (readPtr(obj, f) == value)
            return;
        api.fieldSetValue(obj, f.field, &value);
        if (readPtr(obj, f) == value)
            return;
    }
    *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + f.offset) = value;
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

namespace {

// Four bytes at most, so a caller that keeps four free has room for any code.
size_t putCodePoint(uint32_t code, char* out) {
    if (code < 0x80) {
        out[0] = static_cast<char>(code);
        return 1;
    }
    if (code < 0x800) {
        out[0] = static_cast<char>(0xC0 | (code >> 6));
        out[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (code >> 12));
        out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (code >> 18));
    out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (code & 0x3F));
    return 4;
}

} // namespace

bool toUtf8(void* str, char* out, size_t size) {
    int32_t len = stringLength(str);
    const uint16_t* chars = stringChars(str);
    if (len <= 0 || !chars)
        return false;

    size_t used = 0;
    for (int32_t i = 0; i < len; i++) {
        uint32_t code = chars[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < len) {
            uint32_t low = chars[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (code >= 0xD800 && code <= 0xDFFF)
            code = 0xFFFD;
        if (used + 4 >= size)
            return false;
        used += putCodePoint(code, out + used);
    }
    out[used] = '\0';
    return true;
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
