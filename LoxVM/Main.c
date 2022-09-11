#include <stdio.h>
#include "chunk.h"
#include "debug.h"
#include "vm.h"

void printIntro()
{

}

int32_t getErrorCode(InterpretResult result)
{
	// todo
	return 0;
}

void writeTonsOfConstants(Chunk* chunk)
{
	for (int i = 1; i <= 300; ++i)
	{
		writeConstant(chunk, i, 100 + i);
	}
}

int32_t main(int argc, char* args[])
{
	printIntro();

	VM vm;
	initVM(&vm);

	uint32_t lineNumber = 123;
	Chunk chunk;
	initChunk(&chunk);

	// test
	uint32_t constIndex = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT, lineNumber);
	writeChunk(&chunk, constIndex, lineNumber);

	writeConstant(&chunk, 3.4, lineNumber);
	writeChunk(&chunk, OP_ADD, lineNumber);

	writeConstant(&chunk, 5.6, lineNumber);
	writeChunk(&chunk, OP_DIVIDE, lineNumber);

	writeChunk(&chunk, OP_NEGATE, lineNumber);
	//writeChunk(&chunk, OP_RETURN, lineNumber);
	//writeTonsOfConstants(&chunk);

	disassembleChunk(&chunk, "test chunk");
	InterpretResult result = interpret(&vm, &chunk);

	// teardown
	freeVM(&vm);
	freeChunk(&chunk);

	return getErrorCode(result);
}
