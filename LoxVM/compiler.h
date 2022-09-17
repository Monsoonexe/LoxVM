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
	uint32_t localCount;
	uint32_t scopeDepth;
} Compiler;

bool compile(const char* source, Chunk* chunk);
void initCompiler(Compiler* compiler);
