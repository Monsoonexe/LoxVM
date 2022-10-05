#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
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
	/// <summary>
	/// Lookup key.
	/// </summary>
	Token name;

	/// <summary>
	/// Depth of scope.
	/// </summary>
	uint32_t depth;

	/// <summary>
	/// Flag if is being captured by a closure.
	/// </summary>
	bool isCaptured;
} Local;

typedef struct
{
	/// <summary>
	/// Index into upvalue array
	/// </summary>
	uint8_t index;

	/// <summary>
	/// If false, is upvalue.
	/// </summary>
	bool isLocal;
} Upvalue;

typedef struct
{
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;

} ParseRule;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler
{
	struct Compiler* enclosing;
	ObjectFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int32_t localCount;
	Upvalue upvalues[UINT8_COUNT];
	uint32_t scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;

// prototypes
static void compileDeclaration();
static void compileExpression();
static void compileNamedVariable(Token name, bool canAssign);
static void compileStatement();
static void compileVarDeclaration();
static void declareVariable();
static void defineVariable(uint32_t global);
static bool identifiersEqual(Token* a, Token* b);
static ParseRule* getRule(TokenType type);
static uint32_t parseIdentifierConstant(Token* name);
static void parsePrecedence(Precedence precedence);
static uint32_t parseVariable(const char* errorMessage);

static void initCompiler(Compiler* compiler, FunctionType type)
{
	compiler->enclosing = current; // push compiler
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;

	// create entrypoint (like 'main')
	compiler->function = newFunction();

	current = compiler;
	if (type != TYPE_SCRIPT) // track function name
	{
		// copy string so function can outlive compiler into interpreter
		current->function->name = copyString(parser.previous.start,
			parser.previous.length);
	}

	// claim a temp variable at local[0]
	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->name.start = "";
	local->name.length = 0;
}

static Chunk* currentChunk()
{
	return &current->function->chunk;
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
/// advance if token type matches, else generates syntax error.
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

/// <summary>
/// Marks the variable at the top of the locals stack as initialized.
/// Now is ready for use.
/// </summary>
static void markInitialized()
{
	if (current->scopeDepth == 0) return; // skip globals

	// top of the variable stack is now init'd
	current->locals[current->localCount - 1].depth =
		current->scopeDepth;
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
/// <summary>
/// Used when the constant table exceeds 256 elements and needs more digits in the
/// opcode to address the table.
/// </summary>
static void emitBytesLong(uint8_t opcode, uint32_t index)
{
	emitByte(opcode);
	emitByte((uint8_t)(index >> 16)); // consider '& 0xff'
	emitByte((uint8_t)(index >> 8));
	emitByte((uint8_t)(index >> 0));
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
	if (i >= UINT8_COUNT)
	{	// long (24-bit) index
		emitBytesLong(OP_CONSTANT_LONG, i);
	}
	else
	{	// short (8-bit) index
		emitBytes(OP_CONSTANT, i);
	}
}
static void emitClosure(ObjectFunction* function)
{
	uint32_t constantCount = currentChunk()->count;
	if (constantCount > CONSTANTS_MAX)
	{	// error
		error("Too many constants in one chunk.");
		return;
	}

	// emit constant
	uint32_t i = addConstant(currentChunk(), OBJECT_VAL(function));
	if (i >= UINT8_COUNT)
	{	// long (24-bit) index
		emitBytesLong(OP_CLOSURE_LONG, i);
	}
	else
	{	// short (8-bit) index
		emitBytes(OP_CLOSURE, i);
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

/// <summary>
/// Emits nil and return.
/// </summary>
static void emitReturn()
{
	emitByte(OP_NIL);
	emitByte(OP_RETURN);
}

static ObjectFunction* endCompiler()
{
	emitReturn();
	ObjectFunction* function = current->function; // return value

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError)
		disassembleChunk(currentChunk(), function->name != NULL ?
			function->name->chars : "<script>");
#endif

	current = current->enclosing; // pop compiler
	return function;
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
		if (current->locals[current->localCount - 1].isCaptured)
		{
			// pop locals in runs
			if (count == 1)
				emitByte(OP_POP);
			else if (count > 1)
				emitBytes(OP_POPN, count);
			count = 0; // reset counter

			emitByte(OP_CLOSE_UPVALUE);
		}
		else
		{
			++count;
		}

		--current->localCount;
	}

	// pop locals off stack to deallocate that are left over
	if (count == 1)
		emitByte(OP_POP);
	else if (count > 1)
		emitBytes(OP_POPN, count);
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

static void compileDot(bool canAssign)
{
	consume(TOKEN_IDENTIFIER, "Expected property name after '.'.");
	uint32_t nameIndex = parseIdentifierConstant(&parser.previous);
	uint8_t opCode = 0;

	// assignment or call (setter or getter)?
	if (canAssign && match(TOKEN_EQUAL)) // assignment
	{
		compileExpression();
		opCode = nameIndex < UINT8_COUNT ? OP_SET_PROPERTY : OP_SET_PROPERTY_LONG;
	}
	else // call
	{
		opCode = nameIndex < UINT8_COUNT ? OP_GET_PROPERTY : OP_GET_PROPERTY_LONG;
	}

	// long or short instruction?
	if (nameIndex >= UINT8_COUNT)
		emitBytesLong(opCode, nameIndex);
	else
		emitBytes(opCode, nameIndex);
}

static void compileBreak(bool canAssign)
{
	consume(TOKEN_SEMICOLON, "Expected ';' after 'break'.");
	// emitJump
	//TODO - figure out how many bytes are between me and end of loop and push that many
	//TODO - verify is inside loop
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

static uint8_t compileArgumentList()
{
	uint8_t argCount = 0; // return value
	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			compileExpression();
			if (argCount == UINT8_MAX)
				error("Can't have more than 255 arguments.");
			++argCount;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
	return argCount;
}

static void compileBlock()
{
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		compileDeclaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expected '}' after block.");
}

static void compileCall(bool canAssign)
{
	uint8_t argCount = compileArgumentList();
	emitBytes(OP_CALL, argCount);
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
	compileStatement(); // true body

	uint32_t elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP); // pop condition

	// else branch
	if (match(TOKEN_ELSE))
		compileStatement(); // false body
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

static void compileForStatement()
{
	beginScope();

	// initializer clause
	consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'.");
	if (match(TOKEN_SEMICOLON))
	{
		// no initializer
	}
	else if (match(TOKEN_VAR))
	{
		compileVarDeclaration();
	}
	else
	{
		compileExpressionStatement();
	}
	consume(TOKEN_SEMICOLON, "Expected ';'.");

	// condition clause
	uint32_t loopStart = currentChunk()->count;
	bool hasCondition = false;
	uint32_t exitJump = 0;
	if (!match(TOKEN_SEMICOLON)) // is optional
	{
		hasCondition = true;
		compileExpression();
		consume(TOKEN_SEMICOLON, "Expected ';' after loop condition.");

		// jump out of loop if false
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // discard result of condition
	}

	// incrementer clause
	if (!match(TOKEN_RIGHT_PAREN))
	{
		// b.c. single-pass compiler, jump over inc, do body, then jump back to inc.
		uint32_t bodyJump = emitJump(OP_JUMP);
		uint32_t incrementStart = currentChunk()->count;
		compileExpression();
		emitByte(OP_POP); // discard result of operation
		consume(TOKEN_RIGHT_PAREN, "Expected ')' after for-clauses.");

		emitLoop(loopStart); // skip increment on first pass
		loopStart = incrementStart; // increment first, then check condition
		patchJump(bodyJump);
	}

	// body
	compileStatement();
	emitLoop(loopStart); // loop back

	if (hasCondition)
	{
		patchJump(exitJump);
		emitByte(OP_POP);
	}

	endScope();
}

static void compileReturnStatement()
{
	// just return;
	if (match(TOKEN_SEMICOLON))
	{
		// implicitly return 'nil'
		emitReturn();
	}
	else // return a value;
	{
		compileExpression(); // compile returned value
		consume(TOKEN_SEMICOLON, "Expected ';' after return value.");
		emitByte(OP_RETURN);
	}
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
	else if (match(TOKEN_FOR))
	{
		compileForStatement();
	}
	else if (match(TOKEN_RETURN))
	{
		compileReturnStatement();
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
	uint32_t global = parseVariable("Expected variable name.");

	// must be initialized
	consume(TOKEN_EQUAL, "Expected initialization of variable after declaration.");
	compileExpression();
	consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
	
	defineVariable(global);
}

static void compileFunction(FunctionType type)
{
	// guard against C-stackoverflow
	if (current->scopeDepth > MAX_NESTED_CALLS)
	{
		error("Max nested function calls reached.");
		return;
	}

	Compiler compiler;
	initCompiler(&compiler, type); // set as active compiler for emitters
	beginScope(); // endScope called on exit of function body

	// parameter list
	consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");

	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			++current->function->arity;
			if (current->function->arity > UINT8_MAX)
				errorAtCurrent("Can't have more than 255 parameters.");

			uint32_t constant = parseVariable("Expected parameter name.");
			defineVariable(constant);
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameter list.");

	// body
	consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
	compileBlock();

	// create object
	ObjectFunction* function = endCompiler();
	emitClosure(function);

	// emit upvalues
	for (uint8_t i = 0; i < function->upvalueCount; ++i)
	{
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

static void compileFunDeclaration()
{
	uint32_t global = parseVariable("Expected function name.");

	markInitialized();
	compileFunction(TYPE_FUNCTION); // stores function on stack
	defineVariable(global); //put function in variable
}

static void compileMethod()
{
	// identifier
	consume(TOKEN_IDENTIFIER, "Expected method name.");
	uint32_t constantIndex = parseIdentifierConstant(&parser.previous);

	// closure body
	FunctionType type = TYPE_FUNCTION;
	compileFunction(type); // closure on stack

	// emit bytecode
	if (constantIndex >= UINT8_COUNT)
		emitBytesLong(OP_METHOD_LONG, constantIndex);
	else
		emitBytes(OP_METHOD, constantIndex);


	// bound class
}

static void compileClassDeclaration()
{
	// handle identifier
	consume(TOKEN_IDENTIFIER, "Expected a class name.");
	Token className = parser.previous;
	uint8_t nameConstant = parseIdentifierConstant(&parser.previous);
	declareVariable();

	// bytecode
	emitBytes(OP_CLASS, nameConstant);
	defineVariable(nameConstant);

	// TODO - inheritance

	// push onto stack for method declarations
	compileNamedVariable(className, false);

	// body
	consume(TOKEN_LEFT_BRACE, "Expected '{' before class body.");
	while (!(check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)))
		compileMethod();
	consume(TOKEN_RIGHT_BRACE, "Expected '}' after class body.");
	emitByte(OP_POP); // class 
}

static void compileDeclaration()
{
	// in order of commonality
	if (match(TOKEN_VAR))
		compileVarDeclaration();
	else if (match(TOKEN_FUN))
		compileFunDeclaration();
	else if (match(TOKEN_CLASS))
		compileClassDeclaration();
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
	else if (value == -1) // never gets hit because the '-' is parsed before number
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

static int32_t addUpvalue(Compiler* compiler, uint8_t index, bool isLocal)
{
	uint32_t upvalueCount = compiler->function->upvalueCount;

	// check if already enclosed
	for (uint32_t i = 0; i < upvalueCount; ++i)
	{
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal)
			return i; // already enclosed
	}

	// guard against too many upvalues
	if (upvalueCount >= UINT8_COUNT)
	{
		error("Too many closure variables in function.");
		return 0;
	}

	// create new upvalue
	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

/// <summary>
/// the locals array in the compiler has the exact same layout as the VM's 
/// stack will have at runtime.
/// </summary>
/// <returns>Index of local variable on stack or a 
/// negative number to indicate 'not found'.</returns>
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

	return -1; // not found in locals. try another scope
}

static int32_t resolveUpvalue(Compiler* compiler, Token* name)
{
	// quick exit if at top level
	if (compiler->enclosing == NULL)
		return -1; // must be global (or undefined)

	// resolve local to this function
	int32_t local = resolveLocal(compiler->enclosing, name);
	if (local > -1)
	{
		compiler->enclosing->locals[local].isCaptured = true; // capture
		return addUpvalue(compiler, (uint8_t)local, true);
	}

	// resolve local to outer functions
	int32_t upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue > -1)
		return addUpvalue(compiler, (uint8_t)upvalue, false);

	return -1; // flag not found. check global?
}

static void compileNamedVariable(Token name, bool canAssign)
{
	uint8_t getOp, setOp; // opcode to be emitted
	int32_t arg; // index

	// which scope is the variable located?
	if ((arg = resolveLocal(current, &name)) > -1) // local
	{
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else if ((arg = resolveUpvalue(current, &name)) > -1) // enclosed
	{
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	else // global
	{
		// TODO - account for globals assigned an index > 256
		arg = parseIdentifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	// getter or setter?
	uint8_t opCode; // the operation to actually perform
	if (canAssign && match(TOKEN_EQUAL))
	{
		compileExpression();
		opCode = setOp;
	}
	else
	{
		opCode = getOp;
	}

	// emit code
	if (arg < UINT8_COUNT)
		emitBytes(opCode, (uint8_t)arg);
	else
	{
		// should only get here if it's a global
		error("Long variables are not supported yet. TODO.");
		emitBytesLong(opCode + 1, (uint32_t)arg); // TODO - support LONG variant
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
  [TOKEN_LEFT_PAREN]	= {compileGrouping, compileCall,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]	= {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]			= {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]			= {NULL,     compileDot,   PREC_CALL},
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
  [TOKEN_BREAK]			= {compileBreak,     NULL,   PREC_NONE},
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
	local->isCaptured = false;
}

static bool identifiersEqual(Token* a, Token* b)
{
	uint32_t length = a->length; // fetch once
	return (length == b->length) && memcmp(a->start, b->start, length) == 0;
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

/// <summary>
/// Adds an identifier to the constant table so it is available at runtime.
/// </summary>
static uint32_t parseIdentifierConstant(Token* name)
{
	// return a copy of the string as identifier
	// TODO - take constant string
	ObjectString* lexeme = copyString(name->start, name->length);
	// convert to Value and store in const table
	// TODO - handle LONG, many constants
	return addConstant(currentChunk(), OBJECT_VAL(lexeme)); //makeConstant(OBJECT_VAL(lexeme));
}

static uint32_t parseVariable(const char* errorMessage)
{
	consume(TOKEN_IDENTIFIER, errorMessage);

	// handle local variable
	declareVariable();
	if (current->scopeDepth > 0)
		return 0; // dummy table index

	return parseIdentifierConstant(&parser.previous);
}

static void defineVariable(uint32_t global)
{
	// don't define local variables
	if (current->scopeDepth > 0)
	{
		markInitialized();
		return;
	}

	// TODO - support OP_DEFINE_GLOBAL_LONG
	emitBytes(OP_DEFINE_GLOBAL, global & 0xff);
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
ObjectFunction* compile(const char* source)
{
	uint32_t line = -1;
	Compiler compiler;
	initScanner(source);
	initParser(&parser);
	initCompiler(&compiler, TYPE_SCRIPT);

	advance(); // prime the pump

	while (!match(TOKEN_EOF))
		compileDeclaration();

	ObjectFunction* function = endCompiler();
	return parser.hadError ? NULL : function;
}

void markCompilerRoots()
{
	Compiler* compiler = current;

	// linked-list traversal
	while (compiler != NULL)
	{
		markObject((Object*)compiler->function);
		compiler = compiler->enclosing;
	}
}
