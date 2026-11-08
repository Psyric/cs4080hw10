#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"

static void testCompressedLineLookup(void) {
    Chunk chunk;
    initChunk(&chunk);

    writeChunk(&chunk, OP_RETURN, 10);
    writeChunk(&chunk, OP_RETURN, 10);
    writeChunk(&chunk, OP_RETURN, 11);
    writeChunk(&chunk, OP_RETURN, 11);
    writeChunk(&chunk, OP_RETURN, 10);

    assert(chunk.lineCount == 3);
    assert(getLine(&chunk, 0) == 10);
    assert(getLine(&chunk, 1) == 10);
    assert(getLine(&chunk, 2) == 11);
    assert(getLine(&chunk, 3) == 11);
    assert(getLine(&chunk, 4) == 10);
    assert(getLine(&chunk, -1) == -1);
    assert(getLine(&chunk, 5) == -1);

    freeChunk(&chunk);
    printf("Challenge 1 passed: 5 byte offsets use 3 line runs.\n");
}

static void testConstantInstructionWidths(void) {
    Chunk chunk;
    initChunk(&chunk);

    writeConstant(&chunk, 1.25, 20);
    assert(chunk.code[0] == OP_CONSTANT);
    assert(chunk.code[1] == 0);

    for (int index = 1; index < 255; index++) {
        addConstant(&chunk, (double)index);
    }
    writeConstant(&chunk, 8.5, 20);
    assert(chunk.code[2] == OP_CONSTANT);
    assert(chunk.code[3] == UINT8_MAX);

    writeConstant(&chunk, 9.5, 21);
    assert(chunk.code[4] == OP_CONSTANT_LONG);
    assert(chunk.code[5] == 0);
    assert(chunk.code[6] == 1);
    assert(chunk.code[7] == 0);
    writeChunk(&chunk, OP_RETURN, 21);

    assert(getLine(&chunk, 0) == 20);
    assert(getLine(&chunk, 4) == 21);
    assert(disassembleInstruction(&chunk, 0) == 2);
    assert(disassembleInstruction(&chunk, 2) == 4);
    assert(disassembleInstruction(&chunk, 4) == 8);
    assert(disassembleInstruction(&chunk, 8) == 9);

    freeChunk(&chunk);
    printf("Challenge 2 passed: constant indices 0/255 use short form; 256 uses long form.\n");
}

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--all") == 0)) {
        testCompressedLineLookup();
        testConstantInstructionWidths();
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--challenge1") == 0) {
        testCompressedLineLookup();
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--challenge2") == 0) {
        testConstantInstructionWidths();
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Usage: %s [--all|--challenge1|--challenge2]\n", argv[0]);
    return EXIT_FAILURE;
}