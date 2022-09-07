#include <stdio.h>
#include "chunk.h"
#include "debug.h"

int main(int argc, char* args[])
{
	printf("hello world.");

	Chunk chunk;
	initChunk(&chunk);
	writeChunk(&chunk, OP_RETURN);

	disassembleChunk(&chunk, "test chunk");
	freeChunk(&chunk);

	return 0;
}
