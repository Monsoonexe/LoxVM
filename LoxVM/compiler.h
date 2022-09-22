#pragma once

#include "object.h"
#include "scanner.h"
#include "vm.h"

// test this
#define MAX_NESTED_CALLS UINT16_MAX

typedef struct
{
	Token name;
	uint32_t depth;
} Local;

typedef struct
{
	/// <summary>
	/// Index into upvalue array
	/// </summary>
	uint8_t index;

	/// <summary>
	/// If false, is upvalue.
	/// </summary>
	bool isLocal;
} Upvalue;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_SCRIPT,
} FunctionType;

typedef struct
{
	struct Compiler* enclosing;
	ObjectFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int32_t localCount;
	Upvalue upvalues[UINT8_COUNT];
	uint32_t scopeDepth;
} Compiler;

ObjectFunction* compile(const char* source);
void initCompiler(Compiler* compiler, FunctionType type);
