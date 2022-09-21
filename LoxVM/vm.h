#pragma once

#include "object.h"
#include "nativeFunctions.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

#define STACK_DEFAULT (FRAMES_MAX * 256)

typedef struct
{
	ObjectFunction* function;

	/// <summary>
	/// 
	/// </summary>
	uint8_t* ip; // return address

	/// <summary>
	/// Frame pointer;
	/// </summary>
	Value* slots; // frame pointer locals and args?
} CallFrame;

typedef struct
{
	int64_t exitCode;

	CallFrame callStack[FRAMES_MAX];
	uint32_t frameCount;

	/// <summary>
	/// Points to where the next value will go.
	/// </summary>
	//Value* sp; // stack.count
	ValueArray stack;

	/// <summary>
	/// Global variables.
	/// </summary>
	Table globals;

	/// <summary>
	/// Hash set of interned strings. 'value' is always 'nil' and meaningless.
	/// </summary>
	Table strings;

	/// <summary>
	/// Root of dynamically-allocated objects linked-list.
	/// </summary>
	Object* objects;
} VM;

typedef enum
{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm; // declare, and expose to the rest of the program

void freeVM(VM* vm);
void initStack(VM* vm);
void initVM(VM* vm);
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
