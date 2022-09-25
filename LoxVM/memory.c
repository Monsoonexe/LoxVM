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

static void blackenObject(Object* object);
static void markArray(ValueArray* array);
static void markRoots();
static void traceReferences();

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
	traceReferences();

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
#endif
}

static void blackenObject(Object* object)
{
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	printValue(OBJECT_VAL(object));
	printf("\n");
#endif

	switch (object->type)
	{
		case OBJECT_CLOSURE:
		{
			ObjectClosure* closure = (ObjectClosure*)object;
			markObject((Object*)closure->function);
			for (uint32_t i = 0; i < closure->upvalueCount; ++i)
			{
				markObject((Object*)closure->upvalues[i]);
			}
			break;
		}
		case OBJECT_FUNCTION:
		{
			ObjectFunction* function = (ObjectFunction*)object;
			markObject((Object*)function->name);
			markArray(&function->chunk.constants);
			break;
		}
		case OBJECT_INSTANCE: exit(-1);
		case OBJECT_NATIVE: break;
		case OBJECT_STRING: break;
		case OBJECT_UPVALUE: markValue(((ObjectUpvalue*)object)->closed); break;
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

	free(vm.grayStack);
}
static void markArray(ValueArray* array)
{
	for (uint32_t i = 0; i < array->count; ++i)
	{
		markValue(array->values[i]);
	}
}

void markObject(Object* object)
{
	if (object == NULL || object->isMarked)
		return;

	// print object being marked
#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	printValue(OBJECT_VAL(object));
	printf("\n");
#endif

	object->isMarked = true;

	// challenge: skip adding strings and natives to gray stack
	// since they do not get processed. darken from white to black.
	ObjectType type = object->type;
	if (type == OBJECT_STRING || type == OBJECT_NATIVE)
		return; //  A black object is any object whose isMarked field is set and that is no longer in the gray stack.
	
	//
	if (vm.grayCapacity < vm.grayCount + 1)
	{
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		Object** temp = vm.grayStack; // prevent memory leak warning from realloc
		vm.grayStack = (Object**)realloc(temp, sizeof(Object*) * vm.grayCapacity);

		if (vm.grayStack == NULL)
			exit(1);
	}

	vm.grayStack[vm.grayCount++] = object;
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

static void traceReferences()
{
	while (vm.grayCount > 0)
	{
		Object* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}
}
