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

#define GC_HEAP_GROW_FACTOR 2

static void blackenObject(Object* object);
static void markArray(ValueArray* array);
static void markRoots();
static void sweep();
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
		case OBJECT_CLASS:
		{
			ObjectClass* klass = (ObjectClass*)object;
			freeTable(&klass->methods);
			klass->name = NULL; // GC will handle the string
			FREE(ObjectClass, object);
			break;
		}
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
			function->name = NULL; // GC will handle the string
			FREE(ObjectFunction, object);
			break;
		}
		case OBJECT_INSTANCE:
		{
			ObjectInstance* instance = (ObjectInstance*)object;
			freeTable(&instance->fields); // GC cleans up individual items in table
			instance->_class = NULL; // gc cleans class up
			FREE(ObjectInstance, object);
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
	size_t before = vm.bytesAllocated;
#endif

	markRoots();
	traceReferences();
	tableRemoveWhite(&vm.strings); // sweep string table
	sweep();

	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
	size_t bytesCollected = before - vm.bytesAllocated;
	printf("-- gc end\n");
	if (bytesCollected > 0)
	{
		printf("	collected %zu bytes (from %zu to %zu) next at %zu\n",
			bytesCollected, before, vm.bytesAllocated, vm.nextGC);
	}
#endif
}

/// <summary>
/// Mark each object's children.
/// </summary>
static void blackenObject(Object* object)
{
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	printValue(OBJECT_VAL(object));
	printf("\n");
#endif

	switch (object->type)
	{
		case OBJECT_CLASS:
		{
			ObjectClass* _class = (ObjectClass*)object;
			markObject((Object*)_class->name);
			markTable(&_class->methods);
			break;
		}
		case OBJECT_CLOSURE:
		{
			ObjectClosure* closure = (ObjectClosure*)object;
			markObject((Object*)closure->function);
			for (uint32_t i = 0; i < closure->upvalueCount; ++i)
				markObject((Object*)closure->upvalues[i]);
			break;
		}
		case OBJECT_FUNCTION:
		{
			ObjectFunction* function = (ObjectFunction*)object;
			markObject((Object*)function->name);
			markArray(&function->chunk.constants);
			break;
		}
		case OBJECT_INSTANCE:
		{
			ObjectInstance* instance = (ObjectInstance*)object;
			markObject((Object*)instance->_class);
			markTable(&instance->fields);
			break;
		}
		case OBJECT_NATIVE: break; // goes straight to black
		case OBJECT_STRING: break; // goes straight to black
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
		markValue(array->values[i]);
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
	
	// auto-expand
	if (vm.grayCapacity < vm.grayCount + 1)
	{
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		Object** temp = vm.grayStack; // prevent memory leak warning from realloc
		vm.grayStack = (Object**)realloc(temp, sizeof(Object*) * vm.grayCapacity);

		if (vm.grayStack == NULL)
			exit(1);
	}

	// strings and natives don't need to be processed, so skip graying them
	Value value = OBJECT_VAL(object);
	if (!(IS_NATIVE(value) || IS_STRING(value)))
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
	for (Value* slot = vm.stack.values; slot < &vm.stack.values[vm.stack.count]; ++slot)
		markValue(*slot);

	// mark call stack array
	for (uint32_t i = 0; i < vm.frameCount; ++i)
		markObject((Object*)vm.callStack[i].closure);

	// mark upvalues linked list
	for (ObjectUpvalue* upvalue = vm.openUpvalues;
		upvalue != NULL; upvalue = upvalue->next)
	{
		markObject((Object*)upvalue);
	}

	markTable(&vm.globals);
	markCompilerRoots();
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated += newSize - oldSize;
	if (newSize > oldSize)
	{
#ifdef DEBUG_STRESS_GC
		collectGarbage();
#endif
		if (vm.bytesAllocated > vm.nextGC)
			collectGarbage();
	}

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

static void sweep()
{
	Object* previous = NULL;
	Object* object = vm.objects;

	// traverse linked list
	while (object != NULL)
	{
		// is node still reachable?
		if (object->isMarked) // yes
		{
			object->isMarked = false; // clear flag for next run
			// skip. move to next node
			previous = object;
			object = object->next;
		}
		else // no
		{
			Object* unreached = object;

			// linked-list removal
			object = object->next;
			if (previous != NULL)
				previous->next = object; // re-link nodes
			else
				vm.objects = object; // set head

			freeObject(unreached);
		}
	}
}

static void traceReferences()
{
	while (vm.grayCount > 0)
	{
		Object* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}
	// grey stack is empty
	// every object is either black or white
}
