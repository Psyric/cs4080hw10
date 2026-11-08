#include <stdio.h>

#include "debug.h"
#include "value.h"

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int shortInstruction(const char* name, const Chunk* chunk, int offset) {
    if (offset + 2 >= chunk->count) {
        printf("%-16s <missing operand>\n", name);
        return chunk->count;
    }
    uint16_t slot = (uint16_t)chunk->code[offset + 1] |
        (uint16_t)((uint16_t)chunk->code[offset + 2] << 8);
    printf("%-16s %4u\n", name, slot);
    return offset + 3;
}

    static int jumpInstruction(const char* name, int sign, const Chunk* chunk, int offset) {
        if (offset + 2 >= chunk->count) {
            printf("%-16s <missing operand>\n", name);
            return chunk->count;
        }
        uint16_t jump = (uint16_t)((uint16_t)chunk->code[offset + 1] << 8) |
            chunk->code[offset + 2];
        printf("%-16s %4d -> %d\n", name, offset,
               offset + 3 + sign * (int)jump);
        return offset + 3;
    }

static int constantInstruction(const char* name, const Chunk* chunk, int offset, int operandSize) {
    if (offset + operandSize >= chunk->count) {
        printf("%-20s <missing operand>\n", name);
        return chunk->count;
    }

    uint32_t constant = chunk->code[offset + 1];
    if (operandSize == 3) {
        constant |= (uint32_t)chunk->code[offset + 2] << 8;
        constant |= (uint32_t)chunk->code[offset + 3] << 16;
    }

    printf("%-20s %4u '", name, constant);
    if (constant < (uint32_t)chunk->constants.count) {
        printValue(chunk->constants.values[constant]);
    } else {
        printf("<invalid constant>");
    }
    printf("'\n");
    return offset + operandSize + 1;
}

void disassembleChunk(const Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

int disassembleInstruction(const Chunk* chunk, int offset) {
    printf("%04d ", offset);
    int line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    switch (chunk->code[offset]) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset, 1);
        case OP_CONSTANT_LONG:
            return constantInstruction("OP_CONSTANT_LONG", chunk, offset, 3);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_GET_LOCAL:
            return shortInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return shortInstruction("OP_SET_LOCAL", chunk, offset);
            case OP_DUP:
                return simpleInstruction("OP_DUP", offset);
            case OP_JUMP:
                return jumpInstruction("OP_JUMP", 1, chunk, offset);
            case OP_JUMP_IF_FALSE:
                return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
            case OP_LOOP:
                return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_DEFINE_GLOBAL:
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset, 1);
        case OP_GET_GLOBAL:
            return constantInstruction("OP_GET_GLOBAL", chunk, offset, 1);
        case OP_SET_GLOBAL:
            return constantInstruction("OP_SET_GLOBAL", chunk, offset, 1);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %u\n", chunk->code[offset]);
            return offset + 1;
    }
}