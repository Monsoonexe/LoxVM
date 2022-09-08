#include <stdio.h>
#include "debug.h"
#include "value.h"

static uint32_t constantInstruction(const char* name,
	Chunk* chunk, uint32_t offset)
{
	uint8_t constantIndex = chunk->code[offset + 1];
	printf("%-16s %4d '", name, constantIndex);
	printValue(chunk->constants.values[constantIndex]);
	printf("'\n");
	return offset + 2; // op and index of const
}

static uint32_t simpleInstruction(const char* name, uint32_t offset)
{
	printf("%s\n", name);
	return offset + 1;
}

void disassembleChunk(Chunk* chunk, const char* name)
{
	printf("== %s ==\n", name);

	for (int offset = 0; offset < chunk->count;)
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
int disassembleInstruction(Chunk* chunk, uint32_t offset)
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
	case OP_CONSTANT:
		return constantInstruction("OP_CONSTANT", chunk, offset);
	case OP_RETURN:
		return simpleInstruction("OP_RETURN", offset);
	default:
		printf("Unknown opcode %d\n", instruction);
		return offset + 1;
	}

	return 0;
}
