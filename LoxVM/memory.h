#pragma once
#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) \
	(type*)reallocate(NULL, 0, sizeof(type) * (count))

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
	(type*)reallocate(pointer, sizeof(type) * (oldCount), \
		sizeof(type)* (newCount))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(pointer, sizeof(type) * (oldCount), 0)

void collectGarbage();
void freeObjects(Object* objects);
void markObject(Object* object);
void markValue(Value value);

/// <summary>
/// allocate, free, shrink, or grow. Also keeps accounting of memory.
/// </summary>
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
