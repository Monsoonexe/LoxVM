#pragma once
#include "common.h"

typedef struct Object Object;
typedef struct ObjectString ObjectString;
typedef struct ObjectFunction ObjectFunction;
typedef struct ObjectNative ObjectNative;
typedef struct ObjectClosure ObjectClosure;
typedef struct ObjectUpvalue ObjectUpvalue;
typedef struct ObjectClass ObjectClass;
typedef struct ObjectInstance ObjectInstance;

typedef enum
{
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJECT,
} ValueType;

typedef struct
{
	ValueType type;
	union
	{
		bool boolean;
		double number;
		Object* object;
	} as; // name of union reads like a cast
} Value;

// type checkers
#define IS_BOOL(value)		((value).type == VAL_BOOL)
#define IS_NIL(value)		((value).type == VAL_NIL)
#define IS_NUMBER(value)	((value).type == VAL_NUMBER)
#define IS_OBJECT(value)	((value).type == VAL_OBJECT)

// getters
#define AS_BOOL(value)		((value).as.boolean)
#define AS_NUMBER(value)	((value).as.number)
#define AS_OBJECT(value)	((value).as.object)

// setters
#define BOOL_VAL(value)		((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL				((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)	((Value){VAL_NUMBER, {.number = value}})
#define OBJECT_VAL(obj)		((Value){VAL_OBJECT, {.object = (Object*)obj}})

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
/// Equality comparer ( a == b)
/// </summary>
bool valuesEqual(Value a, Value b);
void writeValueArray(ValueArray* array, Value value);
