#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

inline void initEntry(Entry* entry)
{
	entry->key = NULL;
	entry->value = NIL_VAL;
}

static Entry* findEntry(Entry* entries, uint32_t capacity, ObjectString* key)
{
	uint32_t index = key->hash % capacity; //key->hash & (capacity - 1); // modulo
	Entry* tombstone = NULL;

	// linear probing
	while (true) // non-infinite due to load factor expansion
	{
		// because of string interning, strings are compared by ref
		Entry* entry = &entries[index];
		if (entry->key == NULL)
		{
			// tombstone?
			if (IS_NIL(entry->value))
			{
				return tombstone != NULL ? tombstone : entry;
			}
			else
			{
				if (tombstone == NULL) tombstone = entry;
			}
		}
		else if (entry->key == key) // entry found
		{	
			return entry;
		}

		index = (index + 1) % capacity; // loop back // (index + 1) & (capacity - 1)
	}
}

static void adjustCapacity(Table* table, uint32_t capacity)
{
	// create bucket array
	Entry* entries = ALLOCATE(Entry, capacity);

	// init all entries
	for (uint32_t i = 0; i < capacity; ++i)
		initEntry(&entries[i]);

	// copy over existing elements
	table->count = 0; // reset and count non-tombstones
	uint32_t oldCapacity = table->capacity;
	for (uint32_t i = 0; i < oldCapacity; ++i)
	{
		Entry* entry = &table->entries[i];
		if (entry->key == NULL) continue; // skip empty and tombstone

		// assign new slot
		Entry* dest = findEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(Entry, table->entries, oldCapacity);

	// update table fields
	table->entries = entries;
	table->capacity = capacity;
}

bool tableDelete(Table* table, ObjectString* key)
{
	// handle empty table
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	// tombstone at entry
	entry->key = NULL;
	entry->value = BOOL_VAL(true);
	return true;
}

bool tableGet(Table* table, ObjectString* key, Value* value)
{
	// handle empty table
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL) return false;

	// return values
	*value = entry->value;
	return true;
}

bool tableSet(Table* table, ObjectString* key, Value value)
{
	// ensure capacity
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD_FACTOR)
	{
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	// find bucket
	Entry* entry = findEntry(table->entries, table->capacity, key);
	bool isNew = entry->key == NULL;
	if (isNew && IS_NIL(entry->value)) // new and not tombstone
		++table->count;

	// set entry
	entry->key = key;
	entry->value = value;
	return isNew;
}

void copyTable(Table* src, Table* dest)
{
	for (uint32_t i = 0; i < src->capacity; ++i)
	{
		Entry* entry = &src->entries[i];
		if (entry->key != NULL)
			tableSet(dest, entry->key, entry->value);
	}
}

void freeTable(Table* table)
{
	FREE_ARRAY(Entry, table->entries, table->capacity);
	initTable(table);
}

void initTable(Table* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

float loadFactor(Table* table)
{
	return table->count / (float)table->capacity;
}
