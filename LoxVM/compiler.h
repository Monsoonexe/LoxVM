#pragma once
#include "object.h"
#include "scanner.h"
#include "vm.h"

typedef struct
{
	Token name;
	uint32_t depth;
} Local;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_SCRIPT,
} FunctionType;

typedef struct
{
	ObjectFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	uint32_t scopeDepth;
	int32_t localCount;
} Compiler;

ObjectFunction* compile(const char* source);
void initCompiler(Compiler* compiler, FunctionType type);
