#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
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
static ObjectString* allocateString(char* chars, uint32_t length)
{
	// base constructor
	ObjectString* string = ALLOCATE_OBJECT(ObjectString, OBJECT_STRING);

	// init string object fields
	string->length = length;
	string->chars = chars;
	return string;
}

ObjectString* copyString(const char* chars, uint32_t length)
{
	char* heapChars = ALLOCATE(char, length + 1); // account for \0
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0'; // null-terminated!
	return allocateString(heapChars, length);
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

ObjectString* takeString(char* chars, uint32_t length)
{
	return allocateString(chars, length);
}
