#pragma once

#include "chunk.h"
#include "common.h"
#include "value.h"
#include "vm.h"

#define OBJECT_TYPE(value)		(AS_OBJECT(value)->type)

// type querries
#define IS_FUNCTION(value)		isObjectType(value, OBJECT_FUNCTION)
#define IS_INSTANCE(value)		isObjectType(value, OBJECT_INSTANCE)
#define IS_STRING(value)		isObjectType(value, OBJECT_STRING)

// type casts
#define AS_FUNCTION(value)		((ObjectFunction*)AS_OBJECT(value))
#define AS_STRING(value)		((ObjectString*)AS_OBJECT(value))
#define AS_CSTRING(value)		(((ObjectString*)AS_OBJECT(value))->chars)

typedef enum
{
	OBJECT_FUNCTION,
	OBJECT_INSTANCE,
	OBJECT_STRING,
} ObjectType;

/// <summary>
/// Base type for all objects in Lox.
/// </summary>
struct Object
{
	ObjectType type;
	struct Object* next; //linked list node
};

struct ObjectFunction
{
	Object object; // header
	uint32_t arity;
	Chunk chunk;
	ObjectString* name;
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

/// <summary>
/// Like a default constructor.
/// </summary>
/// <returns>New instance of a function.</returns>
ObjectFunction* newFunction();
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