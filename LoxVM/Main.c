#include <stdio.h>
#include "chunk.h"
#include "debug.h"

void printIntro()
{

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
	writeChunk(&chunk, OP_RETURN, lineNumber);

	disassembleChunk(&chunk, "test chunk");
	freeChunk(&chunk);

	return 0;
}
