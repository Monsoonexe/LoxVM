#pragma once

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define OBJECT_TYPE(value)		(AS_OBJECT(value)->type)

// type querries
#define IS_CLASS(value)			isObjectType(value, OBJECT_CLASS)
#define IS_CLOSURE(value)		isObjectType(value,	OBJECT_CLOSURE)
#define IS_FUNCTION(value)		isObjectType(value, OBJECT_FUNCTION)
#define IS_INSTANCE(value)		isObjectType(value, OBJECT_INSTANCE)
#define IS_NATIVE(value)		isObjectType(value, OBJECT_NATIVE)
#define IS_STRING(value)		isObjectType(value, OBJECT_STRING)
#define IS_UPVALUE(value)		isObjectType(value, OBJECT_UPVALUE)

// type casts
#define AS_CLASS(value)			((ObjectClass*)AS_OBJECT(value))
#define AS_CLOSURE(value)		((ObjectClosure*)AS_OBJECT(value))
#define AS_FUNCTION(value)		((ObjectFunction*)AS_OBJECT(value))
#define AS_INSTANCE(value)		((ObjectInstance*)AS_OBJECT(value))
#define AS_NATIVE(value)		(((ObjectNative*)AS_OBJECT(value))->function)
#define AS_STRING(value)		((ObjectString*)AS_OBJECT(value))
#define AS_CSTRING(value)		(((ObjectString*)AS_OBJECT(value))->chars)

typedef enum
{
	OBJECT_CLASS,
	OBJECT_CLOSURE,
	OBJECT_FUNCTION,
	OBJECT_INSTANCE,
	OBJECT_NATIVE,
	OBJECT_STRING,
	OBJECT_UPVALUE,
} ObjectType;

/// <summary>
/// Base type for all objects in Lox.
/// </summary>
struct Object
{
	ObjectType type;

	/// <summary>
	/// Marked as reachable by the garbage collector.
	/// </summary>
	bool isMarked;
	struct Object* next; //linked list node
};

struct ObjectFunction
{
	Object object; // header
	uint32_t arity;
	uint32_t upvalueCount;
	Chunk chunk;
	ObjectString* name;
};

typedef Value(*NativeFn)(uint8_t argCount, Value* args);

struct ObjectNative
{
	Object object; // header
	// consider const char* name;
	NativeFn function;
};

/// <summary>
/// Underlying string type in Lox.
/// Essentially a 'string' that is always dynamically allocated.
/// </summary>
struct ObjectString // challenge: flag as dynamic or static and account as such when freeing
{
	Object object;
	uint32_t length;
	bool isDynamic;// not implemented
	const char* chars;
	uint32_t hash;
};

struct ObjectUpvalue
{
	Object object;
	Value* location;

	/// <summary>
	/// Heap-allocated.
	/// </summary>
	Value closed;
	struct ObjectUpvalue* next; // linked-list node
};

struct ObjectClosure
{
	Object object;
	ObjectFunction* function;
	ObjectUpvalue** upvalues;
	uint32_t upvalueCount;
};

struct ObjectClass
{
	Object object;
	ObjectString* name;
	Table methods;
};

struct ObjectInstance
{
	Object object;
	ObjectClass* _class;
	Table fields;
};

/// <summary>
/// Constructor for new class.
/// </summary>
/// <param name="name"></param>
ObjectClass* newClass(ObjectString* name);

/// <summary>
/// Constructor for a new ObjectClosure.
/// </summary>
ObjectClosure* newClosure(ObjectFunction* function);

/// <summary>
/// Like a default constructor.
/// </summary>
ObjectFunction* newFunction();

/// <summary>
/// Constructor for a new ObjectInstance.
/// </summary>
/// <returns></returns>
ObjectInstance* newInstance(ObjectClass* _class);

/// <summary>
/// Constructor for a native function.
/// </summary>
/// <param name="function"></param>
ObjectNative* newNativeFunction(NativeFn function);

/// <summary>
/// Constructor for an upvalue.
/// </summary>
/// <returns></returns>
ObjectUpvalue* newUpvalue(Value* slot);

ObjectString* copyString(const char* chars, uint32_t length);
void printObject(Value value);
ObjectString* takeString(const char* chars, uint32_t length);

/// <summary>
/// Takes ownership of string.
/// </summary>
ObjectString* takeConstantString(const char* chars, uint32_t length);

static inline bool isObjectType(Value value, ObjectType type)
{
	return IS_OBJECT(value) && AS_OBJECT(value)->type == type;
}