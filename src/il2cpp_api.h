#pragma once

#include <cstddef>
#include <cstdint>

// The IL2CPP function table is bound once by bindApi(). It is reached through
// an accessor so no mutable table lives at file scope.
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

Api& il2cppApi();

bool bindApi();

// The same simple name can exist in several assemblies, so a class is only
// accepted once one of its methods with the exact name and arity has matched.
void* classOf(const char* name);
void bindClass(const char* name, void* klass);

struct Field {
    void* field;
    size_t offset;
};

bool resolveField(void* klass, const char* name, Field& out);
void* fieldClass(const Field& f);
// For an array field this returns the element class, not the array class.
void* fieldElementClass(const Field& f);

void* readPtr(void* obj, const Field& f);
int32_t readInt(void* obj, const Field& f);
void writeInt(void* obj, const Field& f, int32_t value);
float readFloat(void* obj, const Field& f);
void writeFloat(void* obj, const Field& f, float value);
bool readBool(void* obj, const Field& f);
void writeBool(void* obj, const Field& f, bool value);
void setRef(void* obj, const Field& f, void* value);

int32_t arrayLength(void* arr);
char* arrayData(void* arr);
int32_t stringLength(void* str);
const uint16_t* stringChars(void* str);
void* newUtf16String(const uint16_t* chars, int32_t len);
// IL2CPP text is UTF-16, but log lines and file paths are UTF-8.
bool toUtf8(void* str, char* out, size_t size);
void* staticFieldData(void* klass);

bool nearValue(float value, float target);

// The runtime MethodInfo starts with the resolved code pointer.
void* methodAddr(void* m);
// A method that is just "return" has no body worth hooking.
bool hasBody(void* a);
