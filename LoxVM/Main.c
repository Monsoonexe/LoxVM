#include <stdio.h>
#include "chunk.h"
#include "debug.h"

void printIntro()
{

}

void writeTonsOfConstants(Chunk* chunk)
{
	for (int i = 0; i < 300; ++i)
	{
		writeConstant(chunk, i, 100 + i);
	}
}

int main(int argc, char* args[])
{
	printIntro();

	Chunk chunk;
	uint32_t lineNumber = 123;
	initChunk(&chunk);

	// test
	uint32_t constIndex = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT, lineNumber);
	writeChunk(&chunk, constIndex, lineNumber);
	writeConstant(&chunk, 99.2, lineNumber);
	writeChunk(&chunk, OP_RETURN, lineNumber);
	writeTonsOfConstants(&chunk);

	disassembleChunk(&chunk, "test chunk");
	freeChunk(&chunk);

	return 0;
}
