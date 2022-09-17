#include <stdio.h>
#include "debug.h"
#include "value.h"

static uint32_t byteInstruction(const char* name,
	Chunk* chunk, uint32_t offset)
{
	uint8_t slot = chunk->code[offset + 1];
	printf("%-16s %4d\n", name, slot);
	return offset + 2; // op and index
}

static uint32_t constantInstruction(const char* name,
	Chunk* chunk, uint32_t offset)
{
	uint8_t constantIndex = chunk->code[offset + 1];
	printf("%-16s %4d '", name, constantIndex);
	printValue(chunk->constants.values[constantIndex]);
	printf("'\n");
	return offset + 2; // op and index of const
}

static uint32_t constantLongInstruction(const char* name,
	Chunk* chunk, uint32_t offset)
{
	// decode 3-byte index
	uint8_t* code = chunk->code; // fetch once
	uint8_t hi = code[offset + 1];
	uint8_t mid = code[offset + 2];
	uint8_t low = code[offset + 3];

	// combine
	uint32_t constantIndex = (hi << 16) | (mid << 8) | (low << 0);

	// print
	printf("%-16s %4d '", name, constantIndex);
	printValue(chunk->constants.values[constantIndex]);
	printf("'\n");
	return offset + 4; // op and 3 bytes of index
}

static uint32_t simpleInstruction(const char* name, uint32_t offset)
{
	printf("%s\n", name);
	return offset + 1;
}

void disassembleChunk(Chunk* chunk, const char* name)
{
	printf("== %s ==\n", name);

	for (uint32_t offset = 0; offset < chunk->count;)
	{
		// let function increment b.c. instructions can have different sizes
		offset = disassembleInstruction(chunk, offset);
	}
}

/*
*	== test chunk ==
*	0000  123 OP_CONSTANT         0 '1.2'
*	0002    | OP_RETURN
*/
uint32_t disassembleInstruction(Chunk* chunk, uint32_t offset)
{
	printf("%04d ", offset);

	// handle line number
	if (offset > 0 &&
		(chunk->lines[offset] == chunk->lines[offset - 1]))
	{
		// still on same line
		printf("   | ");
	}
	else
	{
		// print line number
		printf("%4d ", chunk->lines[offset]);
	}

	// decode
	uint8_t instruction = chunk->code[offset];
	switch (instruction)
	{
		// constants
		case OP_CONSTANT:
			return constantInstruction("OP_CONSTANT", chunk, offset);
		case OP_CONSTANT_LONG:
			return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);

		// literals
		case OP_ZERO:
			return simpleInstruction("OP_ZERO", offset);
		case OP_ONE:
			return simpleInstruction("OP_ONE", offset);
		case OP_NEG_ONE:
			return simpleInstruction("OP_NEG_ONE", offset);
		case OP_NIL:
			return simpleInstruction("OP_NIL", offset);
		case OP_TRUE:
			return simpleInstruction("OP_TRUE", offset);
		case OP_FALSE:
			return simpleInstruction("OP_FALSE", offset);

		case OP_POP:
			return simpleInstruction("OP_POP", offset);
		case OP_POPN:
			return constantInstruction("OP_POPN", chunk, offset);
		case OP_GET_LOCAL:
			return byteInstruction("OP_GET_LOCAL", chunk, offset);
		case OP_SET_LOCAL:
			return byteInstruction("OP_SET_LOCAL", chunk, offset);
		case OP_GET_GLOBAL:
			return constantInstruction("OP_GET_GLOBAL", chunk, offset);
		case OP_SET_GLOBAL:
			return constantInstruction("OP_SET_GLOBAL", chunk, offset);
		case OP_DEFINE_GLOBAL:
			return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);

		// boolean
		case OP_EQUAL: 
			return simpleInstruction("OP_EQUAL", offset);
		case OP_GREATER:
			return simpleInstruction("OP_GREATER", offset);
		case OP_LESS:
			return simpleInstruction("OP_LESS", offset);
		case OP_NOT:
			return simpleInstruction("OP_NOT", offset);

		// arithmetic
		case OP_ADD:
			return simpleInstruction("OP_ADD", offset);
		case OP_SUBTRACT:
			return simpleInstruction("OP_SUBTRACT", offset);
		case OP_MULTIPLY:
			return simpleInstruction("OP_MULTIPLY", offset);
		case OP_DIVIDE:
			return simpleInstruction("OP_DIVIDE", offset);
		case OP_NEGATE:
			return simpleInstruction("OP_NEGATE", offset);
		case OP_PRINT:
			return simpleInstruction("OP_PRINT", offset);
		case OP_RETURN:
			return simpleInstruction("OP_RETURN", offset);
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}

	return 0;
}
