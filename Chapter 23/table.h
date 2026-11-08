#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include "object.h"
#include "value.h"

typedef enum {
    ENTRY_EMPTY,
    ENTRY_TOMBSTONE,
    ENTRY_OCCUPIED
} EntryState;

typedef struct {
    Value key;
    Value value;
    EntryState state;
} Entry;

typedef struct {
    int count;
    int used;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif