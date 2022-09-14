#pragma once
#include "common.h"

typedef enum
{
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
} ValueType;

typedef struct
{
	ValueType type;
	union
	{
		bool boolean;
		double number;
	} as; // name of union reads like a cast
} Value;

// type checkers
#define IS_BOOL(value)		((value).type == VAL_BOOL)
#define IS_NIL(value)		((value).type == VAL_NIL)
#define IS_NUMBER(value)	((value).type == VAL_NUMBER)

// getters
#define AS_BOOL(value)		((value).as.boolean)
#define AS_NUMBER(value)	((value).as.number)

// setters
#define BOOL_VAL(value)		((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL()			((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)	((Value){VAL_NUMBER, {.number = value}})

typedef struct
{
	uint32_t capacity;
	uint32_t count;
	Value* values;
} ValueArray;

void freeValueArray(ValueArray* array);
void initValueArray(ValueArray* array);
void printValue(Value value);

/// <summary>
/// bool equality.
/// </summary>
bool valuesEqual(Value a, Value b);
void writeValueArray(ValueArray* array, Value value);
