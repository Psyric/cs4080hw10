#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

#define MAX_CONSTANT_INDEX 0xFFFFFF

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lineCount = 0;
    chunk->lineCapacity = 0;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(LineRun, chunk->lines, chunk->lineCapacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }

    if (chunk->lineCount == 0 || chunk->lines[chunk->lineCount - 1].line != line) {
        if (chunk->lineCapacity < chunk->lineCount + 1) {
            int oldCapacity = chunk->lineCapacity;
            chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
            chunk->lines = GROW_ARRAY(LineRun, chunk->lines, oldCapacity, chunk->lineCapacity);
        }
        chunk->lines[chunk->lineCount].offset = chunk->count;
        chunk->lines[chunk->lineCount].line = line;
        chunk->lineCount++;
    }

    chunk->code[chunk->count++] = byte;
}

int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}

void writeConstant(Chunk* chunk, Value value, int line) {
    if (chunk->constants.count > MAX_CONSTANT_INDEX) {
        fprintf(stderr, "Too many constants in one chunk.\n");
        exit(EXIT_FAILURE);
    }

    int constant = addConstant(chunk, value);
    if (constant <= UINT8_MAX) {
        writeChunk(chunk, OP_CONSTANT, line);
        writeChunk(chunk, (uint8_t)constant, line);
        return;
    }

    writeChunk(chunk, OP_CONSTANT_LONG, line);
    writeChunk(chunk, (uint8_t)(constant & 0xFF), line);
    writeChunk(chunk, (uint8_t)((constant >> 8) & 0xFF), line);
    writeChunk(chunk, (uint8_t)((constant >> 16) & 0xFF), line);
}

int getLine(const Chunk* chunk, int offset) {
    if (offset < 0 || offset >= chunk->count) return -1;

    for (int index = chunk->lineCount - 1; index >= 0; index--) {
        if (chunk->lines[index].offset <= offset) return chunk->lines[index].line;
    }
    return -1;
}