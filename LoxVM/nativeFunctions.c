#include "nativeFunctions.h"

Value clockNative(uint8_t argCount, Value* args)
{
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}
