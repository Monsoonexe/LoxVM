#pragma once
#include "common.h"
#include "value.h"
#include "vm.h"

#define OBJECT_TYPE(value)		(AS_OBJECT(value)->type)

// type querries
#define IS_STRING(value)		isObjectType(value, OBJECT_STRING)
#define IS_INSTANCE(value)		isObjectType(value, OBJECT_INSTANCE)
#define IS_FUNCTION(value)		isObjectType(value, OBJECT_FUNCTION)

// type casts
#define AS_STRING(value)		((ObjectString*)AS_OBJECT(value))
#define AS_CSTRING(value)		(((ObjectString*)AS_OBJECT(value))->chars)

typedef enum
{
	OBJECT_STRING,
	OBJECT_INSTANCE,
	OBJECT_FUNCTION,
} ObjectType;

/// <summary>
/// Base type for all objects in Lox.
/// </summary>
struct Object
{
	ObjectType type;
	struct Object* next; //linked list node
};

/// <summary>
/// Underlying string type in Lox.
/// </summary>
struct ObjectString
{
	Object object;
	uint32_t length;
	char* chars;
};

ObjectString* copyString(const char* chars, uint32_t length);
void printObject(Value value);
ObjectString* takeString(char* chars, uint32_t length);

static inline bool isObjectType(Value value, ObjectType type)
{
	return IS_OBJECT(value) && AS_OBJECT(value)->type == type;
}