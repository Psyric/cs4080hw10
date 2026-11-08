#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "table.h"
#include "vm.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct LoopContext LoopContext;

typedef struct {
    Token name;
    int depth;
    int previousSlot;
    bool isImmutable;
} Local;

typedef struct {
    Local locals[UINT16_COUNT];
    int localCount;
    int scopeDepth;
    Table localBindings;
    LoopContext* loop;
} Compiler;

struct LoopContext {
    int continueTarget;
    int scopeDepth;
    LoopContext* enclosing;
};

static Parser parser;
static Chunk* compilingChunk;
static Compiler compiler;
static Compiler* current;

static Chunk* currentChunk(void) {
    return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) {
        return;
    }

    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        fprintf(stderr, " at '%s'", token->start);
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void advance(void) {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) {
            break;
        }
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
        return;
    }
    currentChunk()->code[offset] = (uint8_t)((jump >> 8) & 0xff);
    currentChunk()->code[offset + 1] = (uint8_t)(jump & 0xff);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        error("Loop body too large.");
    }
    emitByte((uint8_t)((offset >> 8) & 0xff));
    emitByte((uint8_t)(offset & 0xff));
}

static void emitLocal(uint8_t opcode, uint16_t slot) {
    emitByte(opcode);
    emitByte((uint8_t)(slot & 0xFF));
    emitByte((uint8_t)(slot >> 8));
}

static void emitReturn(void) {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler(void) {
    emitReturn();
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

static void beginScope(void) {
    current->scopeDepth++;
}

static void endScope(void) {
    current->scopeDepth--;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        Local* local = &current->locals[current->localCount - 1];
        Value key = OBJ_VAL(copyString(local->name.start, local->name.length));
        if (local->previousSlot >= 0) {
            tableSet(&current->localBindings, key,
                     NUMBER_VAL(local->previousSlot));
        } else {
            tableDelete(&current->localBindings, key);
        }
        emitByte(OP_POP);
        current->localCount--;
    }
}

static void emitLoopLocalCleanup(int scopeDepth) {
    for (int index = current->localCount - 1; index >= 0; index--) {
        if (current->locals[index].depth <= scopeDepth) {
            break;
        }
        emitByte(OP_POP);
    }
}

static void expression(void);
static void declaration(void);
static void statement(void);
static void varDeclaration(bool isImmutable);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static int findLocalSlot(Token* name) {
    Value slot;
    Value key = OBJ_VAL(copyString(name->start, name->length));
    if (!tableGet(&current->localBindings, key, &slot)) {
        return -1;
    }
    return (int)AS_NUMBER(slot);
}

static void addLocal(Token name, bool isImmutable) {
    if (current->localCount == UINT16_COUNT) {
        error("Too many local variables in scope.");
        return;
    }

    int previousSlot = findLocalSlot(&name);
    Local* local = &current->locals[current->localCount];
    local->name = name;
    local->depth = -1;
    local->previousSlot = previousSlot;
    local->isImmutable = isImmutable;

    Value key = OBJ_VAL(copyString(name.start, name.length));
    tableSet(&current->localBindings, key,
             NUMBER_VAL(current->localCount));
    current->localCount++;
}

static void declareVariable(bool isImmutable) {
    if (current->scopeDepth == 0) {
        Value key = OBJ_VAL(copyString(parser.previous.start,
                                       parser.previous.length));
        Value existing;
        if (!isImmutable && tableGet(vmImmutableGlobalTable(), key, &existing)) {
            error("Cannot redeclare an immutable global as mutable.");
        }
        if (isImmutable &&
            (tableGet(vmGlobalTable(), key, &existing) ||
             tableGet(vmImmutableGlobalTable(), key, &existing))) {
            error("Cannot declare an immutable global with an existing name.");
        }
        if (isImmutable) {
            tableSet(vmImmutableGlobalTable(), key, BOOL_VAL(true));
        }
        return;
    }

    int previousSlot = findLocalSlot(&parser.previous);
    if (previousSlot >= 0 &&
        current->locals[previousSlot].depth == current->scopeDepth) {
        error("Already a variable with this name in this scope.");
    }
    addLocal(parser.previous, isImmutable);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0 || current->localCount == 0) {
        return;
    }
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static uint8_t identifierConstant(Token* name) {
    Value identifier = OBJ_VAL(copyString(name->start, name->length));
    for (int index = 0; index < currentChunk()->constants.count; index++) {
        Value constant = currentChunk()->constants.values[index];
        if (IS_STRING(constant) && AS_STRING(constant) == AS_STRING(identifier)) {
            if (index > UINT8_MAX) {
                error("Too many constants in one chunk.");
                return 0;
            }
            return (uint8_t)index;
        }
    }
    return makeConstant(identifier);
}

static uint8_t parseVariable(const char* errorMessage, bool isImmutable) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    declareVariable(isImmutable);
    if (current->scopeDepth > 0) {
        return 0;
    }
    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    (void)canAssign;
    emitConstant(OBJ_VAL(constantString(parser.previous.start + 1,
                                        parser.previous.length - 2)));
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE:
            emitByte(OP_FALSE);
            break;
        case TOKEN_NIL:
            emitByte(OP_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(OP_TRUE);
            break;
        default:
            return;
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_BANG:
            emitByte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(OP_NEGATE);
            break;
        default:
            return;
    }
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:
            emitBytes(OP_EQUAL, OP_NOT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emitByte(OP_EQUAL);
            break;
        case TOKEN_GREATER:
            emitByte(OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emitBytes(OP_LESS, OP_NOT);
            break;
        case TOKEN_LESS:
            emitByte(OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitBytes(OP_GREATER, OP_NOT);
            break;
        case TOKEN_PLUS:
            emitByte(OP_ADD);
            break;
        case TOKEN_MINUS:
            emitByte(OP_SUBTRACT);
            break;
        case TOKEN_STAR:
            emitByte(OP_MULTIPLY);
            break;
        case TOKEN_SLASH:
            emitByte(OP_DIVIDE);
            break;
        default:
            return;
    }
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void namedVariable(Token name, bool canAssign) {
    int local = findLocalSlot(&name);
    uint8_t getOp;
    uint8_t setOp;
    int argument;
    if (local >= 0) {
        Local* binding = &current->locals[local];
        if (binding->depth == -1) {
            error("Can't read local variable in its own initializer.");
        }
        if (canAssign && binding->isImmutable && check(TOKEN_EQUAL)) {
            error("Can't assign to an immutable variable.");
        }
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        argument = local;
    } else {
        argument = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        if (local < 0) {
            Value key = OBJ_VAL(copyString(name.start, name.length));
            Value immutable;
            if (tableGet(vmImmutableGlobalTable(), key, &immutable)) {
                error("Can't assign to an immutable variable.");
            }
        }
        if (local >= 0) {
            emitLocal(setOp, (uint16_t)argument);
        } else {
            emitBytes(setOp, (uint8_t)argument);
        }
    } else {
        if (local >= 0) {
            emitLocal(getOp, (uint16_t)argument);
        } else {
            emitBytes(getOp, (uint8_t)argument);
        }
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void parsePrecedence(Precedence precedence) {
    advance();

    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        if (infixRule == NULL) {
            break;
        }
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static void expression(void) {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void printStatement(void) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void ifStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    if (match(TOKEN_ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

static void expressionStatement(void) {
    expression();
    if (match(TOKEN_SEMICOLON)) {
        emitByte(OP_POP);
    } else if (!check(TOKEN_EOF)) {
        consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    }
}

static void whileStatement(void) {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    LoopContext loop;
    loop.continueTarget = loopStart;
    loop.scopeDepth = current->scopeDepth;
    loop.enclosing = current->loop;
    current->loop = &loop;
    statement();
    current->loop = loop.enclosing;

    emitLoop(loopStart);
    patchJump(exitJump);
    emitByte(OP_POP);
}

static void forStatement(void) {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TOKEN_SEMICOLON)) {
    } else if (match(TOKEN_VAR)) {
        varDeclaration(false);
    } else if (match(TOKEN_LET)) {
        varDeclaration(true);
    } else {
        expressionStatement();
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    LoopContext loop;
    loop.continueTarget = loopStart;
    loop.scopeDepth = current->scopeDepth;
    loop.enclosing = current->loop;
    current->loop = &loop;
    statement();
    current->loop = loop.enclosing;
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }
    endScope();
}

static void continueStatement(void) {
    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
    if (current->loop == NULL) {
        error("Can't use 'continue' outside of a loop.");
        return;
    }
    emitLoopLocalCleanup(current->loop->scopeDepth);
    emitLoop(current->loop->continueTarget);
}

static void switchStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after switch value.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");

    int* endJumps = NULL;
    int endJumpCount = 0;
    int endJumpCapacity = 0;

    while (match(TOKEN_CASE)) {
        emitByte(OP_DUP);
        expression();
        consume(TOKEN_COLON, "Expect ':' after case value.");
        emitByte(OP_EQUAL);
        int nextCase = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
        emitByte(OP_POP);
        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
               !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            statement();
        }

        if (endJumpCount == endJumpCapacity) {
            int oldCapacity = endJumpCapacity;
            endJumpCapacity = GROW_CAPACITY(oldCapacity);
            endJumps = GROW_ARRAY(int, endJumps, oldCapacity, endJumpCapacity);
        }
        endJumps[endJumpCount++] = emitJump(OP_JUMP);
        patchJump(nextCase);
        emitByte(OP_POP);
    }

    if (match(TOKEN_DEFAULT)) {
        consume(TOKEN_COLON, "Expect ':' after 'default'.");
        emitByte(OP_POP);
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            statement();
        }
    } else {
        emitByte(OP_POP);
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch cases.");

    for (int index = 0; index < endJumpCount; index++) {
        patchJump(endJumps[index]);
    }
    FREE_ARRAY(int, endJumps, endJumpCapacity);
}

static void statement(void) {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

static void varDeclaration(bool isImmutable) {
    uint8_t global = parseVariable("Expect variable name.", isImmutable);
    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    defineVariable(global);
}

static void synchronize(void) {
    parser.panicMode = false;
    while (!check(TOKEN_EOF)) {
        if (parser.previous.type == TOKEN_SEMICOLON) {
            return;
        }
        switch (parser.current.type) {
            case TOKEN_VAR:
            case TOKEN_LET:
            case TOKEN_PRINT:
            case TOKEN_LEFT_BRACE:
            case TOKEN_IF:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_SWITCH:
            case TOKEN_CONTINUE:
                return;
            default:
                advance();
        }
    }
}

static void declaration(void) {
    if (match(TOKEN_VAR)) {
        varDeclaration(false);
    } else if (match(TOKEN_LET)) {
        varDeclaration(true);
    } else {
        statement();
    }
    if (parser.panicMode) {
        synchronize();
    }
}

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;
    current = &compiler;
    current->localCount = 0;
    current->scopeDepth = 0;
    current->loop = NULL;
    initTable(&current->localBindings);

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }
    endCompiler();
    bool hadError = parser.hadError;
    freeTable(&current->localBindings);
    current = NULL;
    return !hadError;
}
