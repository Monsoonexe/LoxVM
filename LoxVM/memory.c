#include <stdlib.h>
#include "memory.h"

/// <summary>
/// allocate, free, shrink, or grow.
/// </summary>
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
