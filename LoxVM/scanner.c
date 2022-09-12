#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct
{
	const char* start;
	const char* current;
	uint32_t line;
} Scanner;

Scanner scanner;

void initScanner(const char* source)
{
	scanner.start = source;
	scanner.current = source;
	scanner.line = 1;
}

static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static bool isAlpha(char c)
{
	return (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c == '_');
}

static TokenType checkKeyword(uint32_t start, uint32_t length,
	const char* rest, TokenType type)
{
	// is remainder of string the sequence we predict?
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0)
		return type;

	return TOKEN_IDENTIFIER;
}

static TokenType identifierType()
{
	// is keyword?
	switch (scanner.start[0])
	{
		case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
		case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
		case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
		case 'f':
			if (scanner.current - scanner.start > 1)
			{
				switch (scanner.start[1])
				{
					case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
					case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
					case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
				}
			}
			break;
		case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
		case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
		case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
		case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
		case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
		case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
		case 't': 
			if (scanner.current - scanner.start > 1)
			{
				switch (scanner.start[1])
				{
					case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
					case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
				}
			}
			break;
		case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
		case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	}

	// is identifier
	return TOKEN_IDENTIFIER;
}

static bool isAtEnd()
{
	// source string is expected to be null-terminated
	return *scanner.current == '\0';
}

static char advance()
{
	scanner.current++;
	return scanner.current[-1];
}

static char peek()
{
	return *scanner.current;
}

static char peekNext()
{
	return isAtEnd() ? '\0' : scanner.current[1];
}

static bool matchesNext(char expected)
{
	if (isAtEnd() || *scanner.current != expected)
	{
		return false;
	}
	else
	{
		scanner.current++;
		return true;
	}
}

static Token makeToken(TokenType type)
{
	Token token;
	token.type = type;
	token.start = scanner.start;
	token.length = (uint32_t)(scanner.current - scanner.start);
	token.line = scanner.line;
	return token;
}

static Token errorToken(const char* message)
{
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (uint32_t)strlen(message);
	token.line = scanner.line;
	return token;
}

static void consumeWhitespace()
{
	while (true)
	{
		char c = peek();
		switch (c)
		{
		case '\n':
			++scanner.line;
		case ' ':
		case '\r':
		case '\t':
			advance();
			break; // break switch-case
		case '/': // detect comments
			if (peekNext() == '/')
			{
				while (peek() != '\n' && !isAtEnd())
					advance();
			}
			else
			{
				return;
			}
			break;
		default:
			return;
		}
	}
}

static Token makeNumberToken()
{
	// find end of number literal
	while (isDigit(peek()))
		advance();

	// fraction?
	if (peek() == '.' && isDigit(peekNext()))
	{
		advance(); // consume .
		// find end of number literal
		while (isDigit(peek()))
			advance();
	}

	return makeToken(TOKEN_NUMBER);
}

static Token makeStringToken()
{
	while (peek() != '"' && !isAtEnd())
	{
		if (peek() == '\n')
			++scanner.line;
		advance();
	}

	// handle unterminated string
	if (isAtEnd())
		return errorToken("Unterminated string.");

	// end quote
	advance();
	return makeToken(TOKEN_STRING);
}

static Token makeIdentifierToken()
{
	// locate end of identifier
	while (isAlpha(peek()) || isDigit(peek()))
		advance();
	return makeToken(identifierType());
}

Token scanToken()
{
	consumeWhitespace();
	scanner.start = scanner.current; // fast-forward pointer
	if (isAtEnd())
		return makeToken(TOKEN_EOF);

	// lex
	char c = advance();
	if (isAlpha(c)) return makeIdentifierToken();
	if (isDigit(c)) return makeNumberToken();
	switch (c)
	{
	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case ';':return makeToken(TOKEN_SEMICOLON);
	case ',':return makeToken(TOKEN_COMMA);
	case '.':return makeToken(TOKEN_DOT);
	case '-':return makeToken(TOKEN_MINUS);
	case '+':return makeToken(TOKEN_PLUS);
	case '/':return makeToken(TOKEN_SLASH);
	case '*':return makeToken(TOKEN_STAR);
	case '!': return makeToken(matchesNext('=')
		? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=': return makeToken(matchesNext('=')
		? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<': return makeToken(matchesNext('=')
		? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>': return makeToken(matchesNext('=')
		? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	case '"': return makeStringToken();
	default:
		return errorToken("Unexpected character.");
	}

	return errorToken("Unexpected character.");
}
