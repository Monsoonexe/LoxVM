#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chunk.h"
#include "debug.h"
#include "vm.h"

InterpretResult result;

void printIntro()
{

}

static void repl(VM* vm)
{
	char line[1024];
	while (true)
	{
		printf("> "); // prompt
		
		// TODO - multiline
		if (!fgets(line, sizeof(line), stdin))
		{
			printf("\n");
			break;
		}

		interpret(vm, line);
	}
}

static char* readFile(const char* path)
{
	// FILE* file = fopen(path, "rb");
	FILE* file;
	int err = fopen_s(&file, path, "rb");

	// eat your vegetables
	if (file == NULL)
	{
		fprintf(stderr, "Could not open file <%s>\n", path);
		exit(74);
	}

	// count file size
	fseek(file, 0l, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	// alloc buffer
	char* buffer = (char*)malloc(fileSize + 1);

	if (buffer == NULL)
	{
		fprintf(stderr, "Not enough memory to read <%s>\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

	if (bytesRead < fileSize)
	{
		fprintf(stderr, "Could not read file <%s>\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0'; // null-terminate string

	fclose(file);
	return buffer;
}

static void runFile(VM* vm, const char* path)
{
	char* source = readFile(path);
	result = interpret(vm, source);
	free(source);
}

int32_t getErrorCode(InterpretResult result)
{
	if (result == INTERPRET_COMPILE_ERROR)
		return 65;

	else if (result == INTERPRET_RUNTIME_ERROR)
		return 70;
	
	else
		return 0;
}

void writeTonsOfConstants(Chunk* chunk)
{
	for (int i = 1; i <= 300; ++i)
	{
		writeConstant(chunk, i, 100 + i);
	}
}

int32_t main(int argc, char* argv[])
{
	printIntro();

	VM vm;
	initVM(&vm);

	if (argc == 1)
	{
		repl(&vm);
	}
	else if (argc == 2)
	{
		runFile(&vm, argv[1]);
	}
	else
	{
		fprintf(stderr, "Usage: clox [path]\n");
		return 64;
	}

	// teardown
	freeVM(&vm);

	return getErrorCode(result);
}
