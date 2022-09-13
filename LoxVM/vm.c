#include <stdarg.h>
#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

/// <summary>
/// Reset stack pointer.
/// </summary>
/// <param name="vm"></param>
static void resetStack(VM* vm)
{
	vm->stack.count = 0;
	// no need to actually de-allocate anything
}

void freeVM(VM* vm)
{
	freeChunk(vm->chunk);
	freeValueArray(&vm->stack);
}

void initVM(VM* vm)
{
	vm->chunk = NULL;

	// init stack
	initValueArray(&vm->stack);
	vm->stack.capacity = STACK_DEFAULT;
	vm->stack.values = GROW_ARRAY(Value, vm->stack.values, 0, STACK_DEFAULT);
}

/// <summary>
/// Log error and reset state.
/// </summary>
static void runtimeError(VM* vm, const char* format, ...)
{
	// log message
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	// log line number
	size_t instruction = vm->ip - vm->chunk->code - 1; // prev instruction is culprit
	uint32_t line = vm->chunk->lines[instruction];
	fprintf(stderr, "[line %d] in script\n", line);

	// reset state
	resetStack(vm);
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

void push(VM* vm, Value value)
{
	writeValueArray(&vm->stack, value);
}

Value pop(VM* vm)
{
	return vm->stack.values[--vm->stack.count];
}

static Value peek(VM* vm, uint32_t distance)
{
	uint32_t top = vm->stack.count - 1;
	return vm->stack.values[top - distance];
}

static bool isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(VM* vm)
{
#define READ_BYTE() (*(vm->ip++))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (vm->chunk->constants.values[((READ_BYTE() << 16) | (READ_BYTE() << 8) | (READ_BYTE() << 0))])
#define BINARY_OP(valueType, op) do \
{ \
	if (!IS_NUMBER(peek(vm, 0))) \
	{ \
		runtimeError(vm, "Right-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	else if (!IS_NUMBER(peek(vm, 1))) \
	{ \
		runtimeError(vm, "Left-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	double b = AS_NUMBER(pop(vm)); \
	double a = AS_NUMBER(pop(vm)); \
	push(vm, valueType(a op b)); \
} while (false) \

	// work
	while (1)
	{
#ifdef DEBUG_TRACE_EXECUTION
		printf("        ");

		//for (Value* slot = vm->stack.values; slot < vm->stack.count; slot++)
		for (uint32_t i = 0; i < vm->stack.count; ++i)
		{
			printf("[ ");
			printValue(vm->stack.values[i]);
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
		case OP_CONSTANT: push(vm, READ_CONSTANT()); break;
		case OP_CONSTANT_LONG: push(vm, readConstantLong(vm)); break;// function works, macro doesn't
		case OP_ZERO: push(vm, NUMBER_VAL(0)); break;
		case OP_ONE: push(vm, NUMBER_VAL(1)); break;
		case OP_NIL: push(vm, NIL_VAL()); break;
		case OP_TRUE: push(vm, BOOL_VAL(true)); break;
		case OP_FALSE: push(vm, BOOL_VAL(false)); break;
		case OP_ADD: BINARY_OP(NUMBER_VAL, +); break;
		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
		case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break; // TODO, div by 0
		case OP_NOT: push(vm, BOOL_VAL(isFalsey(pop(vm)))); break;
		case OP_NEGATE:
		{
			// type check
			if (!IS_NUMBER(peek(vm, 0)))
			{
				runtimeError(vm, "Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			uint32_t top = vm->stack.count - 1;
			vm->stack.values[top] = NUMBER_VAL(-AS_NUMBER(vm->stack.values[top])); // challenge: avoid push/pop for unary op
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

#undef BINARY_OP
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult interpret(VM* vm, const char* source)
{
	Chunk chunk;
	initChunk(&chunk);

	if (!compile(source, &chunk))
	{
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm->chunk = &chunk;
	vm->ip = vm->chunk->code;

	InterpretResult result = run(vm);

	freeChunk(&chunk);
	return result;
}