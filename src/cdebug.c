#include "cdebug.h"
#include "cvalue.h"
#include "cobj.h"

void printIndent(int indent) {
    for (int i = 0; i < indent; i++)
        printf("\t");
}

int simpleInstruction(const char *name, int offset) {
    printf("%s", name);
    return offset + 1; // consume opcode
}

int u8OperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%03d]", name, readu8Chunk(chunk, offset + 1));
    return offset + 2;
}

int u16OperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%05d]", name, readu16Chunk(chunk, offset + 1));
    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION));
}

int u8u8OperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%03d] [%03d]", name, readu8Chunk(chunk, offset + 1), readu8Chunk(chunk, offset + 2));
    return offset + 3; // op + u8 + u8
}

int u8u16OperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%03d] [%05d]", name, readu8Chunk(chunk, offset + 1), readu16Chunk(chunk, offset + 2));
    return offset + 4; // op + u8 + u16
}

int constInstruction(const char *name, CChunk *chunk, int offset, int indent) {
    int index = readu16Chunk(chunk, offset + 1);
    printf("%-16s [%05d] - ", name, index);
    CValue val = chunk->constants.values[index];

    printValue(val);

    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION)); // consume opcode + uint
}

// public methods in the cdebug.h header

void disasmChunk(CChunk *chunk, const char *name, int indent) {
    printIndent(indent);
    printf("===[[ %s ]]===\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disasmInstr(chunk, offset, indent);
        printf("\n");
    }
}

int disasmInstr(CChunk *chunk, int offset, int indent) {
    printIndent(indent);
    printf("%04d ", offset);

    INSTRUCTION i = chunk->buf[offset];
    int line = chunk->lineInfo[offset];

    if (offset > 0 && line == chunk->lineInfo[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    switch (i) {
        case OP_LOADCONST: 
            return constInstruction("OP_LOADCONST", chunk, offset, indent);
        case OP_SETGLOBAL:
            return constInstruction("OP_SETGLOBAL", chunk, offset, indent);
        case OP_GETGLOBAL:
            return constInstruction("OP_GETGLOBAL", chunk, offset, indent);
        case OP_SETLOCAL:
            return u8OperandInstruction("OP_SETLOCAL", chunk, offset);
        case OP_GETLOCAL:
            return u8OperandInstruction("OP_GETLOCAL", chunk, offset);
        case OP_SETUPVAL:
            return u8OperandInstruction("OP_SETUPVAL", chunk, offset);
        case OP_GETUPVAL:
            return u8OperandInstruction("OP_GETUPVAL", chunk, offset);
        case OP_PEJMP:
            return u16OperandInstruction("OP_PEJMP", chunk, offset);
        case OP_EJMP:
            return u16OperandInstruction("OP_EJMP", chunk, offset);
        case OP_JMP:
            return u16OperandInstruction("OP_JMP", chunk, offset);
        case OP_JMPBACK:
            return u16OperandInstruction("OP_JMPBACK", chunk, offset);
        case OP_POP:
            return u8OperandInstruction("OP_POP", chunk, offset);
        case OP_CALL:
            return u8u8OperandInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            int index = readu16Chunk(chunk, offset + 1);
            printf("%-16s [%05d] - ", "OP_CLOSURE", index);
            CValue val = chunk->constants.values[index];
            CObjFunction *cobjFunc = (CObjFunction*)cosmoV_readObj(val);
            offset += 3; // we consumed the opcode + u16

            printValue(val);
            printf("\n");
        
            // list the upvalues/locals that are captured
            for (int i = 0; i < cobjFunc->upvals; i++) {
                uint8_t encoding = readu8Chunk(chunk, offset++);
                uint8_t index = readu8Chunk(chunk, offset++);
                printIndent(indent + 1);
                printf("references %s [%d]\n", encoding == OP_GETLOCAL ? "local" : "upvalue", index);
            }

            // print the chunk
            disasmChunk(&cobjFunc->chunk, cobjFunc->name == NULL ? UNNAMEDCHUNK : cobjFunc->name->str, indent+1);
            return offset;
        }
        case OP_CLOSE:
            return simpleInstruction("OP_CLOSE", offset);
        case OP_NEWDICT:
            return u16OperandInstruction("OP_NEWDICT", chunk, offset);
        case OP_INDEX:
            return simpleInstruction("OP_INDEX", offset);
        case OP_NEWINDEX:
            return simpleInstruction("OP_NEWINDEX", offset);
        case OP_NEWOBJECT:
            return u16OperandInstruction("OP_NEWOBJECT", chunk, offset);
        case OP_GETOBJECT:
            return simpleInstruction("OP_GETOBJECT", offset);
        case OP_SETOBJECT:
            return simpleInstruction("OP_SETOBJECT", offset);
        case OP_INVOKE:
            return u8u8OperandInstruction("OP_INVOKE", chunk, offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUB:
            return simpleInstruction("OP_SUB", offset);
        case OP_MULT:
            return simpleInstruction("OP_MULT", offset);
        case OP_DIV:
            return simpleInstruction("OP_DIV", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_COUNT:
            return simpleInstruction("OP_COUNT", offset);
        case OP_CONCAT:
            return u8OperandInstruction("OP_CONCAT", chunk, offset);
        case OP_INCLOCAL:
            return u8u8OperandInstruction("OP_INCLOCAL", chunk, offset);
        case OP_INCGLOBAL:
            return u8u16OperandInstruction("OP_INCGLOBAL", chunk, offset);
        case OP_INCUPVAL:
            return u8u8OperandInstruction("OP_INCLOCAL", chunk, offset);
        case OP_INCINDEX:
            return u8OperandInstruction("OP_INCINDEX", chunk, offset);
        case OP_INCOBJECT:
            return u8u16OperandInstruction("OP_INCOBJECT", chunk, offset);
        case OP_RETURN:
            return u8OperandInstruction("OP_RETURN", chunk, offset);
        default:
            printf("Unknown opcode! [%d]\n", i);
            exit(0);
    }


    return 1;
}