#pragma once

#include "common.h"
#include "value.h"

#define TABLE_MAX_LOAD_FACTOR 0.75f

typedef struct
{
	ObjectString* key;
	Value value;
} Entry;

inline void initEntry(Entry* entry);

typedef struct
{
	uint32_t count;
	uint32_t capacity;
	Entry* entries;

} Table;

bool tableDelete(Table* table, ObjectString* key);

/// <summary>
/// Retrieve an item from the table, or NULL.
/// </summary>
bool tableGet(Table* table, ObjectString* key, Value* value);

/// <summary>
/// Add or set an item in the table. <br/>
/// Returns 'true' if the key is new.
/// </summary>
bool tableSet(Table* table, ObjectString* key, Value value);
void copyTable(Table* src, Table* dest);
ObjectString* tableFindString(Table* table, const char* chars,
	uint32_t length, uint32_t hash);
void freeTable(Table* table);
void initTable(Table* table);
float loadFactor(Table* table);