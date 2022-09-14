#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

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

typedef enum
{
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_TERM,        // + -
	PREC_FACTOR,      // * /
	PREC_UNARY,       // ! -
	PREC_CALL,        // . ()
	PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(); // 'declaration reflects use'

typedef struct
{
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;

} ParseRule;

Parser parser;
Chunk* compilingChunk; // TODO - remove from global state

// prototypes
static void compileExpression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

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

	// special-case values

	// emit constant
	uint32_t i = addConstant(currentChunk(), value);
	if (constantCount >= UINT8_MAX)
	{	// long (24-bit) index
		emitByte(OP_CONSTANT_LONG);
		emitByte((uint8_t)(i >> 16));
		emitByte((uint8_t)(i >> 8));
		emitByte((uint8_t)(i >> 0));
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
#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError)
		disassembleChunk(currentChunk(), "code");
#endif
}

static void compileBinary()
{
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1)); // one higher because ((1 + 2) + 3) + 4 (left-associative)

	switch (operatorType)
	{
		case TOKEN_PLUS: emitByte(OP_ADD); break;
		case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
		case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
		case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
		default: return; // unreachable
	}
}

static void compileTrue()
{
	emitByte(OP_TRUE);
}

static void compileFalse()
{
	emitByte(OP_FALSE);
}

static void compileNil()
{
	emitByte(OP_NIL);
}

static void compileExpression()
{
	parsePrecedence(PREC_ASSIGNMENT); // start at lowest precedence
}

static void compileGrouping()
{
	// groupings have no runtime semantics -- merely exist for 
	// inserting lower-prcedence expressions
	compileExpression(); // recursive call to compile expressions between ()
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void compileNumber()
{	 // parse entire string
	double value = strtod(parser.previous.start, NULL);

	// special cases
	if (value == 0)
		emitByte(OP_ZERO);
	else if (value == 1)
		emitByte(OP_ONE);
	else if (value == -1)
		emitByte(OP_NEG_ONE);
	else
		emitConstant(NUMBER_VAL(value));
}

static void compileUnary()
{
	TokenType operatorType = parser.previous.type;

	// compile operand
	parsePrecedence(PREC_UNARY); // recursive call

	// emit operator instruction
	switch (operatorType)
	{
		case TOKEN_BANG: emitByte(OP_NOT); break;
		case TOKEN_MINUS: emitByte(OP_NEGATE); break;
		default: return; // uncreachable
	}
}

ParseRule rules[] =
{
  [TOKEN_LEFT_PAREN]	= {compileGrouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]			= {compileUnary,    compileBinary, PREC_TERM},
  [TOKEN_PLUS]			= {NULL,     compileBinary, PREC_TERM},
  [TOKEN_SEMICOLON]		= {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]			= {NULL,     compileBinary, PREC_FACTOR},
  [TOKEN_STAR]			= {NULL,     compileBinary, PREC_FACTOR},
  [TOKEN_BANG]			= {compileUnary,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER]		= {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS_EQUAL]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_IDENTIFIER]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING]		= {NULL,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]		= {compileNumber,   NULL,   PREC_NONE},
  [TOKEN_AND]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]			= {compileFalse,     NULL,   PREC_NONE},
  [TOKEN_FOR]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]			= {compileNil,     NULL,   PREC_NONE},
  [TOKEN_OR]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]		= {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]			= {compileTrue,     NULL,   PREC_NONE},
  [TOKEN_VAR]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]			= {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence)
{
	advance(); // first token is ALWAYS	some kind of prefix expression, by definition
	ParseFn prefixRule = getRule(parser.previous.type)->prefix;
	if (prefixRule == NULL)
	{
		error("Expected expression.");
		return;
	}

	prefixRule(); // compileSomething()

	// infix
	while (precedence <= getRule(parser.current.type)->precedence)
	{
		// previously-compiled prefix might be an operand for infix
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule();
	}
}

/// <summary>
/// Looks up precedence of the current operator.
/// </summary>
static ParseRule* getRule(TokenType type)
{
	return &rules[type];
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
