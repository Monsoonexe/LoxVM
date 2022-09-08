#include <stdlib.h>
#include "chunk.h"
#include "memory.h"

uint32_t addConstant(Chunk* chunk, Value value)
{
	writeValueArray(&chunk->constants, value);
	return chunk->constants.count - 1; // index of said constant
}

void freeChunk(Chunk* chunk)
{
	FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	FREE_ARRAY(uint32_t, chunk->lines, chunk->capacity);
	freeValueArray(&chunk->constants); // free constants
	initChunk(chunk); // reset to well-defined state
}

void initChunk(Chunk* chunk)
{
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, uint32_t line)
{
	if (chunk->capacity < chunk->count + 1)
	{
		uint32_t oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(uint8_t, chunk->code,
			oldCapacity, chunk->capacity);
		chunk->lines = GROW_ARRAY(uint32_t, chunk->lines,
			oldCapacity, chunk->capacity);
	}

	uint32_t count = chunk->count++; // fetch once
	chunk->code[count] = byte;
	chunk->lines[count] = line;
}
