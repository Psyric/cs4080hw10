#include <string.h>

#include "memory.h"
#include "table.h"

#define TABLE_MAX_LOAD 0.75

static uint32_t hashValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            return AS_BOOL(value) ? 3u : 2u;
        case VAL_NIL:
            return 1u;
        case VAL_NUMBER: {
            double number = AS_NUMBER(value);
            uint64_t bits;
            if (number == 0) {
                bits = 0;
            } else {
                memcpy(&bits, &number, sizeof(bits));
            }
            return (uint32_t)(bits ^ (bits >> 32));
        }
        case VAL_OBJ:
            return IS_STRING(value) ? AS_STRING(value)->hash : 0;
    }
    return 0;
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
    uint32_t index = hashValue(key) % (uint32_t)capacity;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->state == ENTRY_EMPTY) {
            return tombstone != NULL ? tombstone : entry;
        }
        if (entry->state == ENTRY_TOMBSTONE) {
            if (tombstone == NULL) {
                tombstone = entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }
        index = (index + 1) % (uint32_t)capacity;
    }
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int index = 0; index < capacity; index++) {
        entries[index].key = NIL_VAL;
        entries[index].value = NIL_VAL;
        entries[index].state = ENTRY_EMPTY;
    }

    int oldCount = table->count;
    Entry* oldEntries = table->entries;
    int oldCapacity = table->capacity;
    table->entries = entries;
    table->capacity = capacity;
    table->count = 0;
    table->used = 0;

    for (int index = 0; index < oldCapacity; index++) {
        Entry* oldEntry = &oldEntries[index];
        if (oldEntry->state != ENTRY_OCCUPIED) {
            continue;
        }
        Entry* entry = findEntry(entries, capacity, oldEntry->key);
        entry->key = oldEntry->key;
        entry->value = oldEntry->value;
        entry->state = ENTRY_OCCUPIED;
        table->count++;
        table->used++;
    }

    FREE_ARRAY(Entry, oldEntries, oldCapacity);
    (void)oldCount;
}

void initTable(Table* table) {
    table->count = 0;
    table->used = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

bool tableGet(Table* table, Value key, Value* value) {
    if (table->count == 0) {
        return false;
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->state != ENTRY_OCCUPIED) {
        return false;
    }
    *value = entry->value;
    return true;
}

bool tableSet(Table* table, Value key, Value value) {
    if (table->capacity == 0 || table->used + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        if (capacity <= table->capacity) {
            return false;
        }
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->state != ENTRY_OCCUPIED;
    if (entry->state == ENTRY_EMPTY) {
        table->used++;
    }
    if (isNewKey) {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    entry->state = ENTRY_OCCUPIED;
    return isNewKey;
}

bool tableDelete(Table* table, Value key) {
    if (table->count == 0) {
        return false;
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->state != ENTRY_OCCUPIED) {
        return false;
    }
    entry->key = NIL_VAL;
    entry->value = NIL_VAL;
    entry->state = ENTRY_TOMBSTONE;
    table->count--;
    return true;
}

void tableAddAll(Table* from, Table* to) {
    for (int index = 0; index < from->capacity; index++) {
        Entry* entry = &from->entries[index];
        if (entry->state == ENTRY_OCCUPIED) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) {
        return NULL;
    }

    uint32_t index = hash % (uint32_t)table->capacity;
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->state == ENTRY_EMPTY) {
            return NULL;
        }
        if (entry->state == ENTRY_OCCUPIED && IS_STRING(entry->key)) {
            ObjString* string = AS_STRING(entry->key);
            if (string->length == length && string->hash == hash &&
                memcmp(string->chars, chars, (size_t)length) == 0) {
                return string;
            }
        }
        index = (index + 1) % (uint32_t)table->capacity;
    }
}