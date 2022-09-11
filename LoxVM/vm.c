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

static InterpretResult run(VM* vm)
{
#define READ_BYTE() (*(vm->ip++))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])

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

		uint8_t instruction = READ_BYTE();

		// decode instruction
		// consider “direct threaded code”, “jump table”, and “computed goto”.
		switch (instruction)
		{
		case OP_CONSTANT:
		case OP_CONSTANT_LONG:
		{
			Value constant = READ_CONSTANT();
			push(vm, constant);
			printValue(constant);
			printf("\n");
			break;
		}
		case OP_NEGATE:
		{
			push(vm, -pop(vm));
			break;
		}
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
