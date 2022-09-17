#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

VM vm;

/// <summary>
/// Reset stack pointer.
/// </summary>
/// <param name="vm"></param>
static void resetStack()
{
	vm.stack.count = 0;
	// no need to actually de-allocate anything
}

void initStack(VM* vm)
{
	initValueArray(&vm->stack);
	vm->stack.capacity = STACK_DEFAULT;
	vm->stack.values = GROW_ARRAY(Value, vm->stack.values, 0, STACK_DEFAULT);
}

void freeVM(VM* vm)
{
	// free code block
	if (vm->chunk != NULL)
		freeChunk(vm->chunk);

	// free stack
	freeValueArray(&vm->stack);

	// force GC
	freeObjects(vm->objects);

	// reset fields
	initVM(vm);

	freeTable(&vm->strings);
	freeTable(&vm->globals);
}

void initVM(VM* vm)
{
	vm->chunk = NULL;
	vm->ip = NULL;
	vm->objects = NULL;
	initTable(&vm->strings);
	initTable(&vm->globals);
}

/// <summary>
/// Log error and reset state.
/// </summary>
static void runtimeError(const char* format, ...)
{
	// log message
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	// log line number
	size_t instruction = vm.ip - vm.chunk->code - 1; // prev instruction is culprit
	uint32_t line = vm.chunk->lines[instruction];
	fprintf(stderr, "[line %d] in script\n", line);

	// reset state
	resetStack();
}

static Value readConstantLong()
{
	// decode 3-byte index
	uint8_t hi = (*(vm.ip++));
	uint8_t mid = (*(vm.ip++));
	uint8_t low = (*(vm.ip++));

	// combine
	uint32_t constantIndex = (hi << 16) | (mid << 8) | (low << 0);
	return vm.chunk->constants.values[constantIndex];
}

void push(Value value)
{
	writeValueArray(&vm.stack, value);
}

Value pop()
{
	return vm.stack.values[--vm.stack.count];
}

static Value peek(uint32_t distance)
{
	uint32_t top = vm.stack.count - 1;
	return vm.stack.values[top - distance];
}

static bool isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/// <summary>
/// Concatenates two strings together.
/// </summary>
static void concatenate()
{
	ObjectString* b = AS_STRING(pop());
	ObjectString* a = AS_STRING(pop());

	uint32_t length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1); // \0
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0'; // null-terminated

	ObjectString* result = takeString(chars, length);
	push(OBJECT_VAL(result));
}

static InterpretResult run()
{
#define READ_BYTE() (*(vm.ip++))
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (vm.chunk->constants.values[((READ_BYTE() << 16) | (READ_BYTE() << 8) | (READ_BYTE() << 0))])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) do \
{ \
	if (!IS_NUMBER(peek(0))) \
	{ \
		runtimeError("Right-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	else if (!IS_NUMBER(peek(1))) \
	{ \
		runtimeError("Left-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	double b = AS_NUMBER(pop()); \
	double a = AS_NUMBER(pop()); \
	push(valueType(a op b)); \
} while (false) \

	// work
	while (1)
	{
#ifdef DEBUG_TRACE_EXECUTION
		printf("        ");

		//for (Value* slot = vm->stack.values; slot < vm->stack.count; slot++)
		for (uint32_t i = 0; i < vm.stack.count; ++i)
		{
			printf("[ ");
			printValue(vm.stack.values[i]);
			printf(" ]");
		}

		disassembleInstruction(vm.chunk,
			(uint32_t)(vm.ip - vm.chunk->code)); //offset
#endif

		// decode instruction
		// consider �direct threaded code�, �jump table�, and �computed goto�.
		uint8_t operation = READ_BYTE();
		switch (operation)
		{
		// constants
		case OP_CONSTANT: push(READ_CONSTANT()); break;
		case OP_CONSTANT_LONG: push(readConstantLong()); break;// function works, macro doesn't

		// literals
		case OP_ZERO: push(NUMBER_VAL(0)); break;
		case OP_ONE: push(NUMBER_VAL(1)); break;
		case OP_NEG_ONE: push(NUMBER_VAL(-1)); break;
		case OP_NIL: push(NIL_VAL); break;
		case OP_TRUE: push(BOOL_VAL(true)); break;
		case OP_FALSE: push(BOOL_VAL(false)); break;
		case OP_POP: pop(); break; // discard 
		case OP_GET_GLOBAL:
		{
			ObjectString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value))
			{
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}
		case OP_SET_GLOBAL:
		{
			ObjectString* name = READ_STRING();

			// undefined?
			if (tableSet(&vm.globals, name, peek(0)))
			{
				tableDelete(&vm.globals, name); // undo mistake
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_DEFINE_GLOBAL: // consider LONG constants
		{
			ObjectString* name = READ_STRING();
			tableSet(&vm.globals, name, peek(0)); // can easily redefine globals
			pop(); // discard after to be considerate of GC
			break;
		}

		// boolean
		case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
		case OP_EQUAL:
		{
			Value b = pop();
			Value a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;
		}
		case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
		case OP_LESS: BINARY_OP(BOOL_VAL, < ); break;

		// arithmetic
		case OP_ADD: // BINARY_OP(NUMBER_VAL, +); break;
		{
			if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
			{
				concatenate();
			}
			else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
			{
				double b = AS_NUMBER(pop());
				double a = AS_NUMBER(pop());
				push(NUMBER_VAL(a + b));
			}
			else
			{
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
		case OP_DIVIDE: // BINARY_OP(NUMBER_VAL, /); break;
		{
			if (!IS_NUMBER(peek(0)))
			{
				runtimeError("Right-hand operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			else if (!IS_NUMBER(peek(1)))
			{
				runtimeError("Left-hand operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double b = AS_NUMBER(pop());
			if (b == 0) // div 0
			{
				runtimeError("Divide by zero.");
				return INTERPRET_RUNTIME_ERROR;
			}
			double a = AS_NUMBER(pop());
			double quotient = a / b;
			push(NUMBER_VAL(quotient));
			break;
		}
		case OP_NEGATE:
		{
			// type check
			if (!IS_NUMBER(peek(0)))
			{
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			uint32_t top = vm.stack.count - 1;
			vm.stack.values[top] = NUMBER_VAL(-AS_NUMBER(vm.stack.values[top])); // challenge: avoid push/pop for unary op
			break;
		}
		case OP_PRINT:
		{
			printValue(pop());
			printf("\n");
		}
		case OP_RETURN:
		{
			// exit interpreter
			return INTERPRET_OK;
		}
		default:
			return INTERPRET_RUNTIME_ERROR;
		}
	}

#undef BINARY_OP
#undef READ_STRING
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult interpret(const char* source)
{
	Chunk chunk;
	initChunk(&chunk);

	if (!compile(source, &chunk))
	{
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

	InterpretResult result = run();

	freeChunk(&chunk);
	vm.chunk = NULL;
	return result;
}