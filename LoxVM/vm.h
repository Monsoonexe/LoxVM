#pragma once

#include "object.h"
#include "nativeFunctions.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

#define STACK_DEFAULT (FRAMES_MAX * 256)

typedef struct
{
	/// <summary>
	/// The function currently being called.
	/// </summary>
	ObjectClosure* closure;

	/// <summary>
	/// 
	/// </summary>
	uint8_t* ip; // return address

	/// <summary>
	/// Frame pointer;
	/// </summary>
	Value* slots; // frame pointer locals and args?

	/// <summary>
	/// The stack pointer when the function is called, to be restored
	/// when function exits to deallocate the stack.
	/// </summary>
	uint32_t stackOffset;
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
	/// Linked-list of upvalues that are still on the stack.
	/// </summary>
	ObjectUpvalue* openUpvalues;

	size_t bytesAllocated;
	size_t nextGC;

	/// <summary>
	/// Root of dynamically-allocated objects linked-list.
	/// </summary>
	Object* objects;

	uint32_t grayCount;
	uint32_t grayCapacity;
	Object** grayStack;
} VM;

typedef enum
{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm; // declare, and expose to the rest of the program

void freeVM(VM* vm);
/// <summary>
/// Add all the native functions that the vm offers.
/// </summary>
void initNativeFunctions();
void initStack(VM* vm);
void initVM(VM* vm);
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
inline Value* stackTop();
