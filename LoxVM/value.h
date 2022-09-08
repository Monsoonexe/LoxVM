#pragma once
#include "common.h"

typedef double Value;

typedef struct
{
	uint32_t capacity;
	uint32_t count;
	Value* values;
} ValueArray;

void freeValueArray(ValueArray* array);
void initValueArray(ValueArray* array);
void printValue(Value value);
void writeValueArray(ValueArray* array, Value value);
