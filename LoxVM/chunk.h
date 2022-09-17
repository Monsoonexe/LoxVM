#pragma once

#include "common.h"
#include "value.h"

#define CONSTANTS_MAX 16777216 // (1 << 24)

typedef enum
{
	OP_CONSTANT,
	OP_CONSTANT_LONG,
	OP_ZERO,
	OP_ONE,
	OP_NEG_ONE,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,

	/// <summary>
	/// Pop an item off the stack.
	/// </summary>
	OP_POP,

	/// <summary>
	/// Pop 'n' items off the stack.
	/// </summary>
	OP_POPN,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_GLOBAL, // TODO - LONG
	OP_SET_GLOBAL, // TODO - LONG
	OP_DEFINE_GLOBAL,
	OP_EQUAL,
	OP_GREATER, // TODO: <= >=
	OP_LESS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NOT,
	OP_NEGATE,
	OP_PRINT,
	OP_RETURN,
} OpCode;

typedef struct
{
	uint32_t count;
	uint32_t capacity;
	uint8_t* code;
	uint32_t* lines; // could use run-length encoding
	ValueArray constants;
} Chunk;

uint32_t addConstant(Chunk* chunk, Value value);
void freeChunk(Chunk* chunk);
void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, uint32_t line);
uint32_t writeConstant(Chunk* chunk, Value value, uint32_t line);
