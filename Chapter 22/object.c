#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

static Obj* objects;

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->next = objects;
    objects = object;
    return object;
}

static uint32_t hashString(const char* chars, int length) {
    uint32_t hash = 2166136261u;
    for (int index = 0; index < length; index++) {
        hash ^= (uint8_t)chars[index];
        hash *= 16777619u;
    }
    return hash;
}

static ObjString* allocateString(int length) {
    size_t size = sizeof(ObjString) + (size_t)length + 1;
    ObjString* string = (ObjString*)allocateObject(size, OBJ_STRING);
    string->length = length;
    string->ownsChars = true;
    string->chars = string->data;
    string->data[length] = '\0';
    return string;
}

static void discardUninternedString(ObjString* string) {
    if (objects == (Obj*)string) {
        objects = string->obj.next;
    }
    size_t size = sizeof(ObjString) + (size_t)string->length + 1;
    reallocate(string, size, 0);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(vmStringTable(), chars, length, hash);
    if (interned != NULL) {
        return interned;
    }

    ObjString* string = allocateString(length);
    memcpy((char*)string->chars, chars, (size_t)length);
    string->hash = hash;
    tableSet(vmStringTable(), OBJ_VAL(string), NIL_VAL);
    return string;
}

ObjString* constantString(const char* chars, int length) {
    return copyString(chars, length);
}

ObjString* concatenateStrings(const ObjString* a, const ObjString* b) {
    if (a->length > INT_MAX - b->length) {
        return NULL;
    }

    int length = a->length + b->length;
    ObjString* string = allocateString(length);
    char* chars = (char*)string->chars;
    memcpy(chars, a->chars, (size_t)a->length);
    memcpy(chars + a->length, b->chars, (size_t)b->length);
    string->hash = hashString(chars, length);

    ObjString* interned = tableFindString(vmStringTable(), chars, length, string->hash);
    if (interned != NULL) {
        discardUninternedString(string);
        return interned;
    }
    tableSet(vmStringTable(), OBJ_VAL(string), NIL_VAL);
    return string;
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING: {
            ObjString* string = AS_STRING(value);
            fwrite(string->chars, sizeof(char), (size_t)string->length, stdout);
            break;
        }
    }
}

void freeObjects(void) {
    Obj* object = objects;
    while (object != NULL) {
        Obj* next = object->next;
        switch (object->type) {
            case OBJ_STRING: {
                ObjString* string = (ObjString*)object;
                size_t size = sizeof(ObjString);
                if (string->ownsChars) {
                    size += (size_t)string->length + 1;
                }
                reallocate(object, size, 0);
                break;
            }
        }
        object = next;
    }
    objects = NULL;
}