#include <stdlib.h>

#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

static void markRoots();

/// <summary>
/// Destructor.
/// </summary>
static void freeObject(Object* object)
{
#ifdef DEBUG_LOG_GC
	printf("%p free type %d\n", (void*)object, object->type);
#endif

	switch (object->type)
	{
		case OBJECT_CLOSURE:
		{
			// free array of upvalues
			ObjectClosure* closure = (ObjectClosure*)object;
			FREE_ARRAY(ObjectUpvalue*, closure->upvalues,
				closure->upvalueCount);

			// free self
			FREE(ObjectClosure, object);
			break;
		}
		case OBJECT_FUNCTION:
		{
			ObjectFunction* function = (ObjectFunction*)object;
			freeChunk(&function->chunk);
			FREE(ObjectFunction, object);
			// GC will handle the 'name' string
			break;
		}
		case OBJECT_STRING:
		{
			ObjectString* string = (ObjectString*)object;
			if (string->isDynamic) // only free dynamic strings, not interned strings
			{
				// make sure isDynamic is correct or you'll start freeing
				// interned strings.
				FREE_ARRAY(char, string->chars, string->length + 1); // null-term
			}
			FREE(ObjectString, object); // free 'substruct'
			break;
		}
		case OBJECT_NATIVE:
		{
			FREE(ObjectNative, object);
			break;
		}
		case OBJECT_UPVALUE:
		{
			FREE(ObjectUpvalue, object);
			break;
		}
		default: exit(123); // unreachable
	}
}

void collectGarbage()
{
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
#endif

	markRoots();

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
#endif
}

void freeObjects(Object* objects)
{
	while (objects != NULL)
	{
		Object* next = objects->next; // temp
		freeObject(objects);
		objects = next;
	}
}

void markObject(Object* object)
{
	if (object == NULL)
		return;

	// print object being marked
#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	printValue(OBJECT_VAL(object));
	printf("\n");
#endif

	object->isMarked = true;
}

void markValue(Value value)
{
	if (IS_OBJECT(value))
		markObject(AS_OBJECT(value));
}

static void markRoots()
{
	// mark stack array
	for (Value* slot = vm.stack.values; slot < stackTop(); ++slot)
		markValue(*slot);

	// mark call stack array
	for (int32_t i = 0; i < vm.frameCount; ++i)
		markObject((Object*)vm.callStack[i].closure);

	// mark upvalues linked list
	for (ObjectUpvalue* upvalue = vm.openUpvalues;
		upvalue != NULL; upvalue = upvalue->next)
		markObject((Object*)upvalue);

	markTable(&vm.globals);
	markCompilerRoots();
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
#ifdef DEBUG_STRESS_GC
	if (newSize > oldSize)
	{
		collectGarbage();
	}
#endif

	// free memory
	if (newSize == 0)
	{
		free(pointer);
		return NULL;
	}

	void* result = realloc(pointer, newSize);

	// account for machine out of memory
	if (result == NULL) exit(1);

	return result;
}
