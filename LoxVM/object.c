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
	object->isMarked = false;

	// add to front of list
	object->next = vm.objects;
	vm.objects = object; // set as head

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

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
	push(OBJECT_VAL(string)); // store where gc can reach it
	tableSet(&vm.strings, string, NIL_VAL);
	pop();

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

static void printFunction(ObjectFunction* function)
{
	if (function->name == NULL)
		printf("<script>");
	else
		printf("<fn> %s>", function->name->chars);
}

ObjectBoundMethod* newBoundMethod(Value receiver, ObjectClosure* method)
{
	ObjectBoundMethod* boundMethod = ALLOCATE_OBJECT(ObjectBoundMethod, OBJECT_BOUND_METHOD);
	boundMethod->receiver = receiver;
	boundMethod->method = method;
	return boundMethod;
}

ObjectClass* newClass(ObjectString* name)
{
	ObjectClass* _class = ALLOCATE_OBJECT(ObjectClass, OBJECT_CLASS);
	_class->name = name;
	initTable(&_class->methods);
	return _class;
}

ObjectClosure* newClosure(ObjectFunction* function)
{
	uint32_t upvalueCount = function->upvalueCount; // fetch once

	// create upvalue pointers array
	ObjectUpvalue** upvalues = ALLOCATE(ObjectUpvalue*, upvalueCount);

	// init array of upvalue pointers
	for (uint32_t i = 0; i < upvalueCount; ++i)
		upvalues[i] = NULL;

	// create closure
	ObjectClosure* closure = ALLOCATE_OBJECT(ObjectClosure, OBJECT_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = upvalueCount;
	return closure;
}

ObjectFunction* newFunction()
{
	ObjectFunction* function = ALLOCATE_OBJECT(ObjectFunction, OBJECT_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjectInstance* newInstance(ObjectClass* _class)
{
	ObjectInstance* instance = ALLOCATE_OBJECT(ObjectInstance, OBJECT_INSTANCE);
	instance->_class = _class;
	initTable(&instance->fields);
	return instance;
}

ObjectNative* newNativeFunction(NativeFn function)
{
	ObjectNative* native = ALLOCATE_OBJECT(ObjectNative, OBJECT_NATIVE); // new fn
	native->function = function;
	return native;
}

ObjectUpvalue* newUpvalue(Value* slot)
{
	ObjectUpvalue* upvalue = ALLOCATE_OBJECT(ObjectUpvalue, OBJECT_UPVALUE); // new upvalue
	upvalue->location = slot;
	upvalue->closed = NIL_VAL;
	upvalue->next = NULL;
	return upvalue;
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
	ObjectType type = OBJECT_TYPE(value);
	switch (type)
	{
	case OBJECT_BOUND_METHOD: printFunction(AS_BOUND_METHOD(value)->method->function); break;
		case OBJECT_CLASS: printf("%s", AS_CLASS(value)->name->chars); break;
		case OBJECT_CLOSURE: printFunction(AS_CLOSURE(value)->function); break;
		case OBJECT_FUNCTION: printFunction(AS_FUNCTION(value)); break;
		case OBJECT_INSTANCE: printf("%s instance", 
			AS_INSTANCE(value)->_class->name->chars); break;
		case OBJECT_NATIVE: printf("<native fn>"); break;
		case OBJECT_STRING: printf("%s", AS_CSTRING(value)); break;
		case OBJECT_UPVALUE: printf("upvalue"); break;
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
