#pragma once
#include "chunk.h"
#include "value.h"

#define STACK_DEFAULT 256

typedef struct
{
	/// <summary>
	/// Working set of instructions to execute.
	/// </summary>
	Chunk* chunk;

	/// <summary>
	/// Instruction pointer / program counter. <br/>
	/// Points to the next instruction to be executed.
	/// </summary>
	uint8_t* ip; // consider keeping in a register

	/// <summary>
	/// Points to where the next value will go.
	/// </summary>
	//Value* sp; // stack.count
	ValueArray stack;

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
