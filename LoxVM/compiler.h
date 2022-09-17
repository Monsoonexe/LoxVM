#pragma once
#include "object.h"
#include "scanner.h"
#include "vm.h"

typedef struct
{
	Token name;
	uint32_t depth;
} Local;

typedef struct
{
	Local locals[UINT8_COUNT];
	uint32_t scopeDepth;
	int32_t localCount;
} Compiler;

bool compile(const char* source, Chunk* chunk);
void initCompiler(Compiler* compiler);
