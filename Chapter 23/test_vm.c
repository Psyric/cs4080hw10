#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "table.h"
#include "vm.h"

static void emitConstant(Chunk* chunk, double number) {
    writeConstant(chunk, NUMBER_VAL(number), 1);
}

static double runChunk(Chunk* chunk) {
    Value result;
    assert(interpret(chunk, &result) == INTERPRET_OK);
    assert(IS_NUMBER(result));
    return AS_NUMBER(result);
}

static void testChallenge1(void) {
    Chunk chunk;
    initChunk(&chunk);

    emitConstant(&chunk, 1);
    emitConstant(&chunk, 2);
    writeChunk(&chunk, OP_MULTIPLY, 1);
    emitConstant(&chunk, 3);
    writeChunk(&chunk, OP_ADD, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 5);
    freeChunk(&chunk);

    initChunk(&chunk);
    emitConstant(&chunk, 1);
    emitConstant(&chunk, 2);
    emitConstant(&chunk, 3);
    writeChunk(&chunk, OP_MULTIPLY, 1);
    writeChunk(&chunk, OP_ADD, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 7);
    freeChunk(&chunk);

    initChunk(&chunk);
    emitConstant(&chunk, 3);
    emitConstant(&chunk, 2);
    writeChunk(&chunk, OP_SUBTRACT, 1);
    emitConstant(&chunk, 1);
    writeChunk(&chunk, OP_SUBTRACT, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 0);
    freeChunk(&chunk);

    initChunk(&chunk);
    emitConstant(&chunk, 1);
    emitConstant(&chunk, 2);
    emitConstant(&chunk, 3);
    writeChunk(&chunk, OP_MULTIPLY, 1);
    writeChunk(&chunk, OP_ADD, 1);
    emitConstant(&chunk, 4);
    emitConstant(&chunk, 5);
    writeChunk(&chunk, OP_NEGATE, 1);
    writeChunk(&chunk, OP_DIVIDE, 1);
    writeChunk(&chunk, OP_SUBTRACT, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    double result = runChunk(&chunk);
    assert(result > 7.799999 && result < 7.800001);
    freeChunk(&chunk);

    puts("Challenge 1 passed: all four precedence examples evaluate correctly.");
}

static void testChallenge2(void) {
    Chunk chunk;
    initChunk(&chunk);

    emitConstant(&chunk, 4);
    emitConstant(&chunk, 3);
    emitConstant(&chunk, 0);
    emitConstant(&chunk, 2);
    writeChunk(&chunk, OP_SUBTRACT, 1);
    writeChunk(&chunk, OP_MULTIPLY, 1);
    writeChunk(&chunk, OP_SUBTRACT, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 10);
    freeChunk(&chunk);

    initChunk(&chunk);
    emitConstant(&chunk, 4);
    emitConstant(&chunk, 3);
    emitConstant(&chunk, 2);
    writeChunk(&chunk, OP_NEGATE, 1);
    writeChunk(&chunk, OP_MULTIPLY, 1);
    writeChunk(&chunk, OP_NEGATE, 1);
    writeChunk(&chunk, OP_ADD, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 10);
    freeChunk(&chunk);

    puts("Challenge 2 passed: both equivalent sequences evaluate to 10.");
}

static void testChallenge3(void) {
    Chunk chunk;
    initChunk(&chunk);

    for (int index = 0; index < 1024; index++) {
        emitConstant(&chunk, index);
    }
    writeChunk(&chunk, OP_RETURN, 1);
    assert(runChunk(&chunk) == 1023);
    freeChunk(&chunk);

    puts("Challenge 3 passed: VM grew its stack to execute 1024 live values.");
}

static void testStrings(void) {
    const char literalSource[] = "\"borrowed\"";
    Value result;
    assert(interpretSource(literalSource, &result) == INTERPRET_OK);
    assert(IS_STRING(result));
    assert(AS_STRING(result)->ownsChars);
    assert(strcmp(AS_CSTRING(result), "borrowed") == 0);
    assert(AS_STRING(result)->length == 8);
    ObjString* firstLiteral = AS_STRING(result);
    assert(interpretSource("\"borrowed\"", &result) == INTERPRET_OK);
    assert(AS_STRING(result) == firstLiteral);

    const char concatSource[] = "\"left\" + \" right\"";
    assert(interpretSource(concatSource, &result) == INTERPRET_OK);
    assert(IS_STRING(result));
    assert(AS_STRING(result)->ownsChars);
    assert(AS_CSTRING(result) == AS_STRING(result)->data);
    assert(strcmp(AS_CSTRING(result), "left right") == 0);

    assert(interpretSource("\"same\" == \"same\"", &result) == INTERPRET_OK);
    assert(IS_BOOL(result) && AS_BOOL(result));

    assert(interpretSource("\"a\" + 1", &result) == INTERPRET_RUNTIME_ERROR);
    puts("Strings passed: interned literals, inline storage, concatenation, and equality.");
}

static void testHashTable(void) {
    Table table;
    initTable(&table);
    Value value;

    assert(tableSet(&table, NUMBER_VAL(2), BOOL_VAL(true)));
    assert(tableSet(&table, BOOL_VAL(true), NUMBER_VAL(11)));
    assert(tableSet(&table, NIL_VAL, NUMBER_VAL(12)));
    assert(tableSet(&table, NUMBER_VAL(0), NUMBER_VAL(13)));
    assert(tableGet(&table, NUMBER_VAL(2), &value) && IS_BOOL(value) && AS_BOOL(value));
    assert(tableGet(&table, BOOL_VAL(true), &value) && AS_NUMBER(value) == 11);
    assert(tableGet(&table, NIL_VAL, &value) && AS_NUMBER(value) == 12);
    assert(tableGet(&table, NUMBER_VAL(-0.0), &value) && AS_NUMBER(value) == 13);
    assert(!tableSet(&table, NUMBER_VAL(2), NUMBER_VAL(22)));
    assert(tableGet(&table, NUMBER_VAL(2), &value) && AS_NUMBER(value) == 22);

    ObjString* first = copyString("same key", 8);
    ObjString* duplicate = copyString("same key", 8);
    assert(first == duplicate);
    assert(tableSet(&table, OBJ_VAL(first), NUMBER_VAL(44)));
    assert(tableGet(&table, OBJ_VAL(duplicate), &value) && AS_NUMBER(value) == 44);

    assert(tableSet(&table, NUMBER_VAL(10), NUMBER_VAL(10)));
    assert(tableSet(&table, NUMBER_VAL(18), NUMBER_VAL(18)));
    assert(tableDelete(&table, NUMBER_VAL(10)));
    assert(tableGet(&table, NUMBER_VAL(18), &value) && AS_NUMBER(value) == 18);
    assert(tableSet(&table, NUMBER_VAL(26), NUMBER_VAL(26)));
    assert(tableGet(&table, NUMBER_VAL(26), &value) && AS_NUMBER(value) == 26);
    assert(!tableGet(&table, NUMBER_VAL(10), &value));

    for (int index = 0; index < 200; index++) {
        assert(tableSet(&table, NUMBER_VAL(1000 + index), NUMBER_VAL(index)));
    }
    for (int index = 0; index < 200; index++) {
        assert(tableGet(&table, NUMBER_VAL(1000 + index), &value));
        assert(AS_NUMBER(value) == index);
    }

    Table copy;
    initTable(&copy);
    tableAddAll(&table, &copy);
    assert(tableGet(&copy, NIL_VAL, &value) && AS_NUMBER(value) == 12);
    assert(tableGet(&copy, OBJ_VAL(first), &value) && AS_NUMBER(value) == 44);

    freeTable(&copy);
    freeTable(&table);
    puts("Hash table passed: primitive keys, interning, collisions, tombstones, overwrite, and growth.");
}

static void testGlobals(void) {
    Value result;
    assert(interpretSource("var chapter21_value = 40;", &result) == INTERPRET_OK);
    assert(interpretSource("chapter21_value = chapter21_value + 2; print chapter21_value;", &result) == INTERPRET_OK);
    Value stored;
    assert(tableGet(vmGlobalTable(), OBJ_VAL(copyString("chapter21_value", 15)), &stored));
    assert(IS_NUMBER(stored) && AS_NUMBER(stored) == 42);

    Chunk lateBound;
    initChunk(&lateBound);
    assert(compile("later_global", &lateBound));
    assert(interpretSource("var later_global = 73;", &result) == INTERPRET_OK);
    assert(interpret(&lateBound, &result) == INTERPRET_OK);
    assert(IS_NUMBER(result) && AS_NUMBER(result) == 73);
    freeChunk(&lateBound);

    assert(interpretSource("chapter21_missing", &result) == INTERPRET_RUNTIME_ERROR);

    Chunk invalidAssignment;
    initChunk(&invalidAssignment);
    assert(!compile("1 + chapter21_value = 3", &invalidAssignment));
    freeChunk(&invalidAssignment);

    char repeatedNames[6000] = "";
    for (int index = 0; index < 300; index++) {
        strcat(repeatedNames, "print repeated_name;\n");
    }
    Chunk deduplicated;
    initChunk(&deduplicated);
    assert(compile(repeatedNames, &deduplicated));
    int repeatedConstantCount = 0;
    for (int index = 0; index < deduplicated.constants.count; index++) {
        Value constant = deduplicated.constants.values[index];
        if (IS_STRING(constant) && strcmp(AS_CSTRING(constant), "repeated_name") == 0) {
            repeatedConstantCount++;
        }
    }
    assert(repeatedConstantCount == 1);
    freeChunk(&deduplicated);

    puts("Globals passed: declaration, assignment, late binding, errors, and name-constant deduplication.");
}

static void testLocals(void) {
    Value result;
    assert(interpretSource("{ let fixed_local = 5; print fixed_local; }", &result) == INTERPRET_OK);
    assert(interpretSource(
        "{ var scoped_value = 10; { var scoped_value = 20; print scoped_value; } print scoped_value; }",
        &result) == INTERPRET_OK);

    Chunk selfInitializer;
    initChunk(&selfInitializer);
    assert(!compile("{ var self_value = self_value; }", &selfInitializer));
    freeChunk(&selfInitializer);

    Chunk immutableLocal;
    initChunk(&immutableLocal);
    assert(!compile("{ let fixed_value = 1; fixed_value = 2; }", &immutableLocal));
    freeChunk(&immutableLocal);

    assert(interpretSource("let chapter22_constant = 8;", &result) == INTERPRET_OK);
    Chunk immutableGlobal;
    initChunk(&immutableGlobal);
    assert(!compile("chapter22_constant = 9;", &immutableGlobal));
    freeChunk(&immutableGlobal);

    char manyLocals[24000] = "{";
    size_t used = 1;
    for (int index = 0; index < 300; index++) {
        int written = snprintf(manyLocals + used, sizeof(manyLocals) - used,
                               "var local_%d;", index);
        assert(written > 0 && (size_t)written < sizeof(manyLocals) - used);
        used += (size_t)written;
    }
    int written = snprintf(manyLocals + used, sizeof(manyLocals) - used,
                           "local_299 = 299; print local_299;}");
    assert(written > 0 && (size_t)written < sizeof(manyLocals) - used);
    assert(interpretSource(manyLocals, &result) == INTERPRET_OK);

    puts("Locals passed: indexed lookup, shadowing, initializer checks, let assignment checks, and 300 live locals.");
}

static void testControlFlow(void) {
    Value result;
    const char* branches =
        "if (true) print 1; else print 2;"
        "if (false) print 3; else print 4;"
        "print false or 5; print true and 6;";
    assert(interpretSource(branches, &result) == INTERPRET_OK);

    const char* shortCircuit =
        "var short_value = 0;"
        "false and (short_value = 1);"
        "true or (short_value = 2);"
        "print short_value;";
    assert(interpretSource(shortCircuit, &result) == INTERPRET_OK);

    const char* loops =
        "var while_index = 0;"
        "while (while_index < 3) { print while_index; while_index = while_index + 1; }"
        "for (var for_index = 0; for_index < 5; for_index = for_index + 1) {"
        "if (for_index == 2) continue; print for_index; }";
    assert(interpretSource(loops, &result) == INTERPRET_OK);

    const char* switchSource =
        "switch (2) { case 1: print 10; case 2: print 20; case 3: print 30; default: print 40; }"
        "switch (9) { case 1: print 10; default: print 99; }";
    assert(interpretSource(switchSource, &result) == INTERPRET_OK);

    const char* continueScopes =
        "var continue_index = 0;"
        "while (continue_index < 3) {"
        "{ var scoped_temp = continue_index; continue_index = continue_index + 1;"
        "if (scoped_temp < 2) continue; print scoped_temp; } }";
    assert(interpretSource(continueScopes, &result) == INTERPRET_OK);

    const char* continueSwitch =
        "var switch_index = 0;"
        "while (switch_index < 3) {"
        "switch (switch_index) {"
        "case 1: { var skipped_local = 1; switch_index = switch_index + 1; continue; }"
        "default: switch_index = switch_index + 1; }"
        "print switch_index; }";
    assert(interpretSource(continueSwitch, &result) == INTERPRET_OK);

    Chunk invalidContinue;
    initChunk(&invalidContinue);
    assert(!compile("continue;", &invalidContinue));
    freeChunk(&invalidContinue);

    puts("Control flow passed: branches, short-circuiting, loops, switch, and continue scope cleanup.");
}

int main(int argc, char** argv) {
    initVM();
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--all") == 0)) {
        testChallenge1();
        testChallenge2();
        testChallenge3();
        testStrings();
        testHashTable();
        testGlobals();
        testLocals();
        testControlFlow();
    } else if (argc == 2 && strcmp(argv[1], "--challenge1") == 0) {
        testChallenge1();
    } else if (argc == 2 && strcmp(argv[1], "--challenge2") == 0) {
        testChallenge2();
    } else if (argc == 2 && strcmp(argv[1], "--challenge3") == 0) {
        testChallenge3();
    } else if (argc == 2 && strcmp(argv[1], "--strings") == 0) {
        testStrings();
    } else if (argc == 2 && strcmp(argv[1], "--table") == 0) {
        testHashTable();
    } else if (argc == 2 && strcmp(argv[1], "--globals") == 0) {
        testGlobals();
    } else if (argc == 2 && strcmp(argv[1], "--locals") == 0) {
        testLocals();
    } else if (argc == 2 && strcmp(argv[1], "--control-flow") == 0) {
        testControlFlow();
    } else {
        fprintf(stderr, "Usage: %s [--all|--challenge1|--challenge2|--challenge3|--strings|--table|--globals|--locals|--control-flow]\n", argv[0]);
        freeVM();
        return EXIT_FAILURE;
    }
    freeVM();
    return EXIT_SUCCESS;
}