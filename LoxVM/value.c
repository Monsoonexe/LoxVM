#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

typedef void (*PrintValueFn)(Value value);

static void printBoolValue(Value value)
{
	printf(AS_BOOL(value) ? "true" : "false");
}

static void printNilValue(Value value)
{
	printf("nil");
}

static void printNumberValue(Value value)
{
	// g: Print a double in either normal or exponential notation, 
	// whichever is more appropriate for its magnitude.
	printf("%g", AS_NUMBER(value));
}

void initValueArray(ValueArray* array)
{
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

#ifndef NAN_BOXING

PrintValueFn printValueFunctions[] =
{
	[VAL_BOOL] = { printBoolValue },
	[VAL_NIL] = { printNilValue },
	[VAL_NUMBER] = { printNumberValue },
	[VAL_OBJECT] = { printObject }
};

#endif

void printValue(Value value)
{
#ifdef NAN_BOXING
	if (IS_BOOL(value))
	{
		printf(AS_BOOL(value) ? "true" : "false");
	}
	else if (IS_NIL(value))
	{
		printf("nil");
	}
	else if (IS_NUMBER(value))
	{
		printf("%g", AS_NUMBER(value));
	}
	else if (IS_OBJECT(value))
	{
		printObject(value);
	}
	else
	{
		exit(123);
	}
#else
	// lookup table (instead of switch-case)
	ValueType type = value.type;
	PrintValueFn printer = printValueFunctions[type];
	printer(value);
#endif
}

bool valuesEqual(Value a, Value b)
{
#ifdef NAN_BOXING
	return a == b;

#else
	if (a.type != b.type)
		return false;

	switch (a.type) // have same type
	{
		case VAL_BOOL:	 return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:	 return true; // nil :== nil
		case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJECT: return AS_OBJECT(a) == AS_OBJECT(b);
		default: exit(123); // unreachable;
	}
#endif
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
