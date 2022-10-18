#pragma once
#include "common.h"

#define NAN_BOXING

typedef struct Object Object;
typedef struct ObjectString ObjectString;
typedef struct ObjectFunction ObjectFunction;
typedef struct ObjectNative ObjectNative;
typedef struct ObjectClosure ObjectClosure;
typedef struct ObjectUpvalue ObjectUpvalue;
typedef struct ObjectClass ObjectClass;
typedef struct ObjectInstance ObjectInstance;
typedef struct ObjectBoundMethod ObjectBoundMethod;

#ifdef NAN_BOXING

// all exponent bits, the quiet bit, and the special intel bit
#define QNAN     ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT ((uint64_t)0x8000000000000000)

#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

typedef uint64_t Value;

static double valueToNum(Value value)
{
	// Hooray type punning!
	union
	{
		uint64_t bits;
		double num;
	} data;
	data.bits = value;
	return data.num;
}

static Value numToValue(double number)
{
	// Hooray type punning!
	union
	{
		uint64_t bits;
		double num;
	} data;
	data.num = number;
	return (Value)data.bits;
}

#define NIL_VAL				((Value)(uint64_t)(QNAN | TAG_NIL))
#define FALSE_VAL			((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL			((Value)(uint64_t)(QNAN | TAG_TRUE))
#define BOOL_VAL(b)			((b) ? TRUE_VAL : FALSE_VAL)
#define NUMBER_VAL(num)		numToValue(num)
#define OBJECT_VAL(obj)		(Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_OBJECT(value)	((Object*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))
#define AS_NUMBER(value)	valueToNum(value)

#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL)
#define IS_OBJECT(value)	(((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_NIL(value)       ((value) == NIL_VAL)
#define IS_NUMBER(value)	(((value) & QNAN) != QNAN)

#else

// REMEMBER to modify to printValueFunctions table if modifying.
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

#endif

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
