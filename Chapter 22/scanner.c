#include <string.h>

#include "scanner.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

static Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
}

static char advance(void) {
    return *scanner.current++;
}

static char peek(void) {
    return *scanner.current;
}

static char peekNext(void) {
    if (*scanner.current == '\0') {
        return '\0';
    }
    return scanner.current[1];
}

static bool match(char expected) {
    if (peek() != expected) {
        return false;
    }
    scanner.current++;
    return true;
}

static void skipWhitespace(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && peek() != '\0') {
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if ((scanner.current - scanner.start) == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
    switch (scanner.start[0]) {
        case 'a':
            return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c':
            return checkKeyword(1, 4, "lass", TOKEN_CLASS);
        case 'e':
            return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return checkKeyword(2, 2, "r", TOKEN_FOR);
                    case 'u':
                        return checkKeyword(2, 2, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i':
            return checkKeyword(1, 2, "f", TOKEN_IF);
        case 'l':
            return checkKeyword(1, 2, "et", TOKEN_LET);
        case 'n':
            return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o':
            return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p':
            return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r':
            return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v':
            return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            return checkKeyword(1, 4, "hile", TOKEN_WHILE);
        default:
            break;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifier(void) {
    while (isAlphaNumeric(peek())) {
        advance();
    }
    return identifierType();
}

static Token string(void) {
    while (peek() != '"' && peek() != '\0') {
        if (peek() == '\n') {
            scanner.line++;
        }
        advance();
    }

    if (peek() == '\0') {
        return errorToken("Unterminated string.");
    }

    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
    skipWhitespace();
    scanner.start = scanner.current;

    if (peek() == '\0') {
        return makeToken(TOKEN_EOF);
    }

    if (isAlpha(peek())) {
        return makeToken(identifier());
    }

    char c = advance();

    if (isDigit(c)) {
        while (isDigit(peek())) {
            advance();
        }

        if (peek() == '.' && isDigit(peekNext())) {
            advance();
            while (isDigit(peek())) {
                advance();
            }
        }

        return makeToken(TOKEN_NUMBER);
    }

    switch (c) {
        case '"': return string();
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        default: return errorToken("Unexpected character.");
    }
}
