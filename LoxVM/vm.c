#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "vm.h"

void freeVM(VM* vm)
{
	freeChunk(vm->chunk);
}

void initVM(VM* vm)
{
	vm->chunk = NULL;
	vm->sp = vm->stack; // reset stack pointer
}

static Value readConstantLong(VM* vm)
{
	// decode 3-byte index
	uint8_t hi = (*(vm->ip++));
	uint8_t mid = (*(vm->ip++));
	uint8_t low = (*(vm->ip++));

	// combine
	uint32_t constantIndex = (hi << 16) | (mid << 8) | (low << 0);
	return vm->chunk->constants.values[constantIndex];
}

static InterpretResult run(VM* vm)
{
#define READ_BYTE() (*(vm->ip++))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (vm->chunk->constants.values[((READ_BYTE() << 16) | (READ_BYTE() << 8) | (READ_BYTE() << 0))])
#define BINARY_OP(op) do \
{ \
	Value b = pop(vm); \
	Value a = pop(vm); \
	push(vm, a op b); \
} while (false) \

	// work
	while (1)
	{
#ifdef DEBUG_TRACE_EXECUTION
		printf("        ");
		for (Value* slot = vm->stack; slot < vm->sp; slot++)
		{
			printf("[ ");
			printValue(*slot);
			printf(" ]");
		}

		disassembleInstruction(vm->chunk,
			(uint32_t)(vm->ip - vm->chunk->code)); //offset
#endif

		// decode instruction
		// consider “direct threaded code”, “jump table”, and “computed goto”.
		uint8_t operation = READ_BYTE();
		switch (operation)
		{
		case OP_CONSTANT_LONG:
		{
			Value constant = readConstantLong(vm); // function works, macro doesn't
			push(vm, constant);
			printValue(constant);
			printf("\n");
			break;
		}
		case OP_CONSTANT:
		{
			Value constant = READ_CONSTANT();
			push(vm, constant);
			printValue(constant);
			printf("\n");
			break;
		}
		case OP_ADD: BINARY_OP(+); break;
		case OP_SUBTRACT: BINARY_OP(-); break;
		case OP_MULTIPLY: BINARY_OP(*); break;
		case OP_DIVIDE: BINARY_OP(/); break;
		case OP_NEGATE: vm->sp[-1] = -vm->sp[-1]; break;
		case OP_RETURN:
		{
			Value value = pop(vm);
			printValue(value);
			printf("\n");
			return INTERPRET_OK;
		}
		default:
			return INTERPRET_RUNTIME_ERROR;
		}
	}

#undef BINARY_OP
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult interpret(VM* vm, Chunk* chunk)
{
	vm->chunk = chunk;
	vm->ip = chunk->code;
	return run(vm);
}

void push(VM* vm, Value value)
{
	*(vm->sp++) = value;
}

Value pop(VM* vm)
{
	return *(--(vm->sp));
}
