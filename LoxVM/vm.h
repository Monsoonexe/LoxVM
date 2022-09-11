#pragma once
#include "chunk.h"
#include "value.h"

#define STACK_MAX 1024

typedef struct
{
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
} VM;

typedef enum
{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

void freeVM(VM* vm);
void initVM(VM* vm);
InterpretResult interpret(VM* vm, Chunk* chunk);
void push(VM* vm, Value value);
Value pop(VM* vm);
