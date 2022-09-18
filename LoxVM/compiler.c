#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef void (*ParseFn)(bool canAssign); // 'declaration reflects use'

typedef struct
{
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;

} ParseRule;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk; // TODO - remove from global state

// prototypes
static void compileExpression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void compileStatement();
static void compileDeclaration();
static uint8_t parseVariable(const char* errorMessage);
static void defineVariable(uint8_t global);
static uint32_t parseIdentifierConstant(Token* name);
static int32_t resolveLocal(Compiler* compiler, Token* name);

void initCompiler(Compiler* compiler)
{
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	current = compiler;
}

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

static bool check(TokenType type)
{
	return parser.current.type == type;
}

/// <summary>
/// Advances if true.
/// </summary>
static bool match(TokenType type)
{
	if (!check(type))
		return false;
	advance();
	return true;
}

static void patchJump(uint32_t offset)
{
	// -2 to adjust for the jump offset itself
	uint32_t jump = currentChunk()->count - offset - 2;
	if (jump > UINT16_MAX)
		error("Jump is too far away. Consider implementing a JUMP_LONG instruction.");

	// update 16 bits now that the address is known
	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
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
static uint32_t emitJump(OpCode instruction)
{
	emitByte(instruction);

	// emit placeholders for jump offset
	emitByte(0xff);
	emitByte(0xff); 
	return currentChunk()->count - 2;
}
static void emitLoop(uint32_t loopStart)
{
	emitByte(OP_LOOP);

	// get backwards target
	uint32_t offset = currentChunk()->count - loopStart + 2; // skip args
	if (offset > UINT16_MAX)
		error("Loop body too large.");

	emitBytes(((offset >> 8) & 0xff), (offset & 0xff));
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

static void beginScope()
{
	++current->scopeDepth;
}

static void endScope()
{
	--current->scopeDepth;

	// count locals to be deallocated
	uint8_t count = 0;
	while (current->localCount > 0
		&& current->locals[current->localCount - 1].depth > current->scopeDepth)
	{
		++count;
		--current->localCount;
	}

	// pop locals off stack to deallocate
	if (count == 1)
	{
		emitByte(OP_POP);
	}
	else if (count > 1)
	{
		emitBytes(OP_POPN, count);
	}
}

static void synchronize()
{
	// clear panic falg
	parser.panicMode = false;

	// consume until expression terminates
	while (parser.current.type != TOKEN_EOF)
	{
		if (parser.previous.type == TOKEN_SEMICOLON) return;

		// look for statement beginners
		switch (parser.current.type)
		{
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
			return;
		default:
			; // Do nothing.
		}

		advance();
	}
}

static void compileAnd(bool canAssign)
{
	uint32_t endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP); // consume condition
	parsePrecedence(PREC_AND); // right-hand expression
	
	patchJump(endJump);
}

static void compileOr(bool canAssign)
{
	uint32_t elseJump = emitJump(OP_JUMP_IF_FALSE); // b.c. we don't have OP_JUMP_IF_TRUE
	uint32_t endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR); // right-hand expression
	patchJump(endJump);
}

static void compileBinary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1)); // one higher because ((1 + 2) + 3) + 4 (left-associative)

	switch (operatorType)
	{
		// boolean (TODO - !=, <=, and >=)
		case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
		case TOKEN_GREATER:       emitByte(OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
		case TOKEN_LESS:          emitByte(OP_LESS); break;
		case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;

		// arithmetic
		case TOKEN_PLUS: emitByte(OP_ADD); break;
		case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
		case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
		case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
		default: exit(123); // unreachable;
	}
}

static void compileTrue(bool canAssign)
{
	emitByte(OP_TRUE);
}

static void compileFalse(bool canAssign)
{
	emitByte(OP_FALSE);
}

static void compileNil(bool canAssign)
{
	emitByte(OP_NIL);
}

static void compileExpression()
{
	parsePrecedence(PREC_ASSIGNMENT); // start at lowest precedence
}

static void compileBlock()
{
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		compileDeclaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expected '}' after block.");
}

static void compileExpressionStatement()
{
	compileExpression();
	consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
	emitByte(OP_POP); // discard result of expression
}

static void compilePrintStatement()
{
	compileExpression();
	consume(TOKEN_SEMICOLON, "Expected ';' after value.");
	emitByte(OP_PRINT);
}

static void compileIfStatement()
{
	// condition
	consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
	compileExpression();
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");
	
	// handle branching
	uint32_t thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP); // pop condition
	compileStatement();

	uint32_t elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP); // pop condition

	// else branch
	if (match(TOKEN_ELSE))
		compileStatement();
	patchJump(elseJump);
}

static void compileWhileStatement()
{
	// loop target
	uint32_t loopStart = currentChunk()->count; // re-evaluate condition each pass

	// condition
	consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
	compileExpression();
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

	// handle jump
	uint32_t exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP); // pop condition
	compileStatement(); // body
	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP); // pop condition
}

static void compileStatement()
{
	if (match(TOKEN_PRINT))
	{
		compilePrintStatement();
	}
	else if (match(TOKEN_IF))
	{
		compileIfStatement();
	}
	else if (match(TOKEN_WHILE))
	{
		compileWhileStatement();
	}
	else if (match(TOKEN_LEFT_BRACE))
	{
		beginScope();
		compileBlock();
		endScope();
	}
	else
	{
		compileExpressionStatement();
	}
}

static void compileVarDeclaration()
{
	uint8_t global = parseVariable("Expected variable name.");

	// must be initialized
	consume(TOKEN_EQUAL, "Expected initialization of variable after declaration.");
	compileExpression();
	consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
	
	defineVariable(global);
}

static void compileDeclaration()
{
	if (match(TOKEN_VAR))
		compileVarDeclaration();
	else
		compileStatement();

	// recover from panic mode
	if (parser.panicMode)
		synchronize();
}

static void compileGrouping(bool canAssign)
{
	// groupings have no runtime semantics -- merely exist for 
	// inserting lower-prcedence expressions
	compileExpression(); // recursive call to compile expressions between ()
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void compileNumber(bool canAssign)
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

static void compileString(bool canAssign)
{
	// could handle escape characters here
	// trim leading/trailing quotes
	const char* start = parser.previous.start + 1;
	uint32_t end = parser.previous.length - 2;
	emitConstant(OBJECT_VAL(copyString(start, end)));
}

static void compileNamedVariable(Token name, bool canAssign)
{
	uint8_t getOp, setOp; // bytecode to be emitted
	int32_t arg = resolveLocal(current, &name);

	// local or global resolution
	if (arg == -1)
	{
		arg = parseIdentifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}
	else
	{
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}

	// getter or setter?
	if (canAssign && match(TOKEN_EQUAL))
	{
		compileExpression();
		emitBytes(setOp, (uint8_t)arg);
	}
	else
	{
		emitBytes(getOp, (uint8_t)arg);
	}
}

static void compileVariable(bool canAssign)
{
	compileNamedVariable(parser.previous, canAssign);
}

static void compileUnary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;

	// compile operand
	parsePrecedence(PREC_UNARY); // recursive call

	// emit operator instruction
	switch (operatorType)
	{
		case TOKEN_BANG: emitByte(OP_NOT); break;
		case TOKEN_MINUS: emitByte(OP_NEGATE); break;
		default: exit(123); // unreachable;
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
  [TOKEN_BANG_EQUAL]	= {NULL,     compileBinary,   PREC_EQUALITY},
  [TOKEN_EQUAL]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]	= {NULL,     compileBinary,   PREC_EQUALITY},
  [TOKEN_GREATER]		= {NULL,     compileBinary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     compileBinary,   PREC_COMPARISON},
  [TOKEN_LESS]			= {NULL,     compileBinary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]	= {NULL,     compileBinary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]	= {compileVariable,     NULL,   PREC_NONE},
  [TOKEN_STRING]		= {compileString,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]		= {compileNumber,   NULL,   PREC_NONE},
  [TOKEN_QUESTION]		= {NULL,	NULL,	PREC_NONE},
  [TOKEN_COLON]			= {NULL,	NULL,	PREC_NONE},
  [TOKEN_AND]			= {NULL,     compileAnd,   PREC_AND},
  [TOKEN_CLASS]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]			= {compileFalse,     NULL,   PREC_NONE},
  [TOKEN_FOR]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]			= {compileNil,     NULL,   PREC_NONE},
  [TOKEN_OR]			= {NULL,     compileOr,   PREC_OR},
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

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign); // compileSomething()

	// infix
	while (precedence <= getRule(parser.current.type)->precedence)
	{
		// previously-compiled prefix might be an operand for infix
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL))
		error("Invalid assignment target.");
}

static void addLocal(Token name)
{
	// guard against overflow
	if (current->localCount == UINT8_COUNT)
	{
		error("Too many local variables in function.");
		return;
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name; // original source lexeme
	local->depth = -1; // flag as uninit'd
}

static bool identifiersEqual(Token* a, Token* b)
{
	uint32_t length = a->length; // fetch once
	return (length == b->length) && memcmp(a->start, b->start, length) == 0;
}

/// <summary>
/// the locals array in the compiler has the exact same layout as the VM's 
/// stack will have at runtime.
/// </summary>
/// <returns>Index of local variable on stack.</returns>
static int32_t resolveLocal(Compiler* compiler, Token* name)
{
	// walk backwards to get inner-most scope first
	for (int32_t i = compiler->localCount - 1; i >= 0; --i)
	{
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name))
		{
			// ensure fully defined
			if (local->depth == -1)
				error("Can't read local variable in its own initializer.");

			return i;
		}

	}

	// not found in locals -- global
	return -1;
}

static void declareVariable()
{
	// don't declare globals (late bound)
	if (current->scopeDepth == 0)
		return;

	Token* name = &parser.previous;

	// guard against re-declaring variable
	for (int32_t i = current->localCount - 1; i >= 0; --i)
	{
		Local* local = &current->locals[i]; // iterator
		if (local->depth != -1 && local->depth < current->scopeDepth)
			break;

		if (identifiersEqual(name, &local->name))
			error("Already a variable with this name in this scope.");
	}

	addLocal(*name);
}

static uint32_t parseIdentifierConstant(Token* name)
{
	// return a copy of the string as identifier
	// TODO - take constant string
	ObjectString* lexeme = copyString(name->start, name->length);
	// convert to Value and store in const table
	// TODO - handle LONG, many constants
	return addConstant(compilingChunk, OBJECT_VAL(lexeme)); //makeConstant(OBJECT_VAL(lexeme));
}

static uint8_t parseVariable(const char* errorMessage)
{
	consume(TOKEN_IDENTIFIER, errorMessage);

	// handle local variable
	declareVariable();
	if (current->scopeDepth > 0)
		return 0; // dummy table index

	return parseIdentifierConstant(&parser.previous);
}

/// <summary>
/// Marks the variable at the top of the locals stack as initialized.
/// Now is ready for use.
/// </summary>
static void markInitialized()
{
	// top of the variable stack is now init'd
	current->locals[current->localCount - 1].depth =
		current->scopeDepth;
}

static void defineVariable(uint8_t global)
{
	// don't define local variables
	if (current->scopeDepth > 0)
	{
		markInitialized();
		return;
	}

	emitBytes(OP_DEFINE_GLOBAL, global);
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
	Compiler compiler;
	compilingChunk = chunk;
	initScanner(source);
	initParser(&parser);
	initCompiler(&compiler);

	advance(); // prime the pump

	while (!match(TOKEN_EOF))
		compileDeclaration();

	endCompiler();
	return !parser.hadError;
}
