#pragma once

#include "common.h"
#include "value.h"

#define CONSTANTS_MAX 16777216 // (1 << 24)

typedef enum
{
	// constants
	OP_CONSTANT,
	OP_CONSTANT_LONG,

	// literals
	OP_ZERO,
	OP_ONE,
	OP_NEG_ONE, // doesn't actually work
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

	/// <summary>
	/// Supports up to 256 locals.
	/// </summary>
	OP_GET_LOCAL,

	/// <summary>
	/// Supports up to 256 locals.
	/// </summary>
	OP_SET_LOCAL,
	OP_DEFINE_GLOBAL,
	OP_GET_GLOBAL,
	//OP_GET_GLOBAL_LONG, // TODO
	OP_SET_GLOBAL,
	//OP_SET_GLOBAL_LONG, // TODO

	/// <summary>
	/// Get a local from an enclosing scope.
	/// </summary>
	OP_GET_UPVALUE,

	/// <summary>
	/// Set a local from an enclosing scope.
	/// </summary>
	OP_SET_UPVALUE,

	// properties
	OP_GET_PROPERTY,
	OP_GET_PROPERTY_LONG,
	OP_SET_PROPERTY,
	OP_SET_PROPERTY_LONG,

	// assignment
	OP_EQUAL,

	// boolean
	OP_GREATER, // TODO: <= >=
	OP_LESS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NOT,
	OP_NEGATE,

	// control statements
	
	/// <summary>
	/// Conditional jump forward.
	/// </summary>
	OP_JUMP_IF_FALSE,

	/// <summary>
	/// Unconditional jump forward.
	/// </summary>
	OP_JUMP,

	/// <summary>
	/// Conditional backwards jump.
	/// </summary>
	OP_LOOP,

	/// <summary>
	/// Function call.
	/// </summary>
	OP_CALL,

	OP_CLOSURE,
	OP_CLOSURE_LONG,

	/// <summary>
	/// Take variable from stack and put it on heap.
	/// </summary>
	OP_CLOSE_UPVALUE,

	OP_PRINT,
	OP_RETURN,

	// classes
	OP_CLASS,
	OP_METHOD,
	OP_METHOD_LONG,

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
