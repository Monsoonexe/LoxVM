
#include "table.h"

float loadFactor(Table* table)
{
	return table->count / (float)table->capacity;
}
