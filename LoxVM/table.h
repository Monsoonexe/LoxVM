#pragma once

#include "common.h"
#include "value.h"

typedef struct
{
	ObjectString* key;
	Value value;
} Entry;

typedef struct
{
	uint32_t count;
	uint32_t capacity;
	Entry* entries;

} Table;

float loadFactor(Table* table);