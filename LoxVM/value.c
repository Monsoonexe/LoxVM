#include <stdio.h>
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array)
{
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

void printValue(Value value)
{
	switch (value.type)
	{
		case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false");break;
		case VAL_NIL: printf("nil"); break;
		// g: Print a double in either normal or exponential notation, whichever is more appropriate for its magnitude.
		case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
	}
}

bool valuesEqual(Value a, Value b)
{
	if (a.type != b.type)
		return false;

	switch (a.type) // have same type
	{
		case VAL_BOOL:	return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:	return true; // nil := nil
		case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
		default: return false; // unreachable
	}
}

void writeValueArray(ValueArray* array, Value value)
{
	// resize
	if (array->capacity < array->count + 1)
	{
		uint32_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(Value, array->values,
			oldCapacity, array->capacity);
	}

	array->values[array->count++] = value;
}

void freeValueArray(ValueArray* array)
{
	FREE_ARRAY(Value, array->values, array->capacity);
	initValueArray(array);
}
