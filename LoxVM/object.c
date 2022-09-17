#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJECT(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType)

/// <summary>
/// Constructor for Object. Allocates full-sized struct,
/// then inits Object's payload fields.
/// </summary>
static Object* allocateObject(size_t size, ObjectType type)
{
	Object* object = (Object*)reallocate(NULL, 0, size);
	object->type = type;

	// set head
	object->next = vm.objects;
	vm.objects = object;
	return object;
}

/// <summary>
/// Constructor for ObjectString.
/// </summary>
static ObjectString* allocateString(const char* chars, 
	uint32_t length, bool isDynamic, uint32_t hash)
{
	// base constructor
	ObjectString* string = ALLOCATE_OBJECT(ObjectString, OBJECT_STRING);

	// init string object fields
	string->length = length;
	string->chars = chars;
	string->isDynamic = isDynamic;
	string->hash = hash;

	// intern
	tableSet(&vm.strings, string, NIL_VAL);

	return string;
}

static uint32_t hashString(const char* key, uint32_t length)
{
	// FNV-1a hash function
	// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	uint32_t hash = 2166136261u; // return value
	for (uint32_t i = 0; i < length; ++i)
	{
		// maybe hash 4 bytes at a time?
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}

	return hash;
}

ObjectString* copyString(const char* chars, uint32_t length)
{
	// clone c-string
	uint32_t hash = hashString(chars, length);

	// interned string?
	ObjectString* internedString = tableFindString(&vm.strings, chars, length, hash);
	if (internedString != NULL) return internedString;

	// new string
	char* heapChars = ALLOCATE(char, length + 1); // account for \0
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0'; // null-terminated!

	// new StringObject()
	return allocateString(heapChars, length, true, hash);
}

void printObject(Value value)
{
	switch (OBJECT_TYPE(value))
	{
		case OBJECT_STRING:
			printf("%s", AS_CSTRING(value));
			break;
		case OBJECT_FUNCTION:
		case OBJECT_INSTANCE:
			printf("%d", OBJECT_TYPE(value));
			break;
		default: exit(123); // unreachable
	}
}

ObjectString* takeString(const char* chars, uint32_t length)
{
	uint32_t hash = hashString(chars, length);

	// interned string?
	ObjectString* internedString = tableFindString(&vm.strings, chars, length, hash);
	if (internedString == NULL)
	{
		return allocateString(chars, length, true, hash);
	}
	else
	{
		FREE_ARRAY(char, chars, length + 1);
		return internedString;
	}
}

ObjectString* takeConstantString(const char* chars, uint32_t length)
{
	// TODO - figure out weird bug
	uint32_t hash = hashString(chars, length);

	// interned string?
	ObjectString* internedString = tableFindString(&vm.strings, chars, length, hash);
	if (internedString == NULL)
	{
		return allocateString(chars, length, false, hash);
	}
	else
	{
		FREE_ARRAY(char, chars, length + 1);
		return internedString;
	}
}
