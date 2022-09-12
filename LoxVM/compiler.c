#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

typedef struct
{
	/// <summary>
	/// compilation error.
	/// </summary>
	bool hadError;
	bool panicMode;
	Token current;
	Token previous;

} Parser;

Parser parser;
Chunk* compilingChunk; // TODO - remove from global state

static Chunk* currentChunk()
{
	return compilingChunk; // don't ask where I got it from
}

static void initParser(Parser* parser)
{
	parser->hadError = false;
	parser->panicMode = false;
}

static void errorAt(Token* token, const char* message)
{
	// handle panic mode
	if (parser.panicMode) 
		return;
	parser.panicMode = true;

	if (token->type == TOKEN_EOF)
	{
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR)
	{
		// nada
	}
	else
	{
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

static void error(const char* message)
{
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message)
{
	errorAt(&parser.current, message);
}

static void advance()
{
	parser.previous = parser.current;

	while (true)
	{
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR)
			break;
		
		errorAtCurrent(parser.current.start);
	}
}

/// <summary>
/// Generates syntax errors.
/// </summary>
static void consume(TokenType type, const char* message)
{
	if (parser.current.type == type)
	{
		advance(parser);
		return;
	}

	errorAtCurrent(message);
}

static void emitByte(uint8_t byte)
{
	writeChunk(currentChunk(), byte, parser.previous.line);
}
static void emitBytes(uint8_t byte1, uint8_t byte2)
{
	emitByte(byte1);
	emitByte(byte2);
}
static void emitConstant(Value value)
{
	uint32_t constantCount = currentChunk()->count;
	if (constantCount > CONSTANTS_MAX)
	{	// error
		error("Too many constants in one chunk.");
		return;
	}

	// emit constant
	uint32_t i = addConstant(currentChunk(), value);
	if (constantCount >= UINT8_MAX)
	{	// long (24-bit) index
		emitbyte(OP_CONSTANT_LONG);
		emitbyte((uint8_t)(i >> 16));
		emitbyte((uint8_t)(i >> 8));
		emitbyte((uint8_t)(i >> 0));
	}
	else
	{	// short (8-bit) index
		emitBytes(OP_CONSTANT, i);
	}
}
static void emitReturn()
{
	emitByte(OP_RETURN);
}

static void endCompiler()
{
	emitReturn();
}

static void compileNumber()
{
	double value = strtod(parser.previous.start, NULL); // parse entire string
	emitConstant(value);
}

static void compileExpression()
{

}

/// <summary>
/// Main
/// </summary>
bool compile(const char* source, Chunk* chunk)
{
	uint32_t line = -1;
	compilingChunk = chunk;
	initScanner(source);
	initParser(&parser);

	advance(); // prime the pump
	compileExpression();
	consume(TOKEN_EOF, "Expected end of expression.");
	endCompiler();
	return !parser.hadError;
}
