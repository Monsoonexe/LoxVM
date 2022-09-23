#include <stdlib.h>

#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

/// <summary>
/// Destructor.
/// </summary>
static void freeObject(Object* object)
{
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

void freeObjects(Object* objects)
{
	while (objects != NULL)
	{
		Object* next = objects->next; // temp
		freeObject(objects);
		objects = next;
	}
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
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
