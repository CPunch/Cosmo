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

int shortOperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%03d]", name, readu8Chunk(chunk, offset + 1));
    return offset + 2;
}

int longOperandInstruction(const char *name, CChunk *chunk, int offset) {
    printf("%-16s [%05d]", name, readu16Chunk(chunk, offset + 1));
    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION));
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
            return shortOperandInstruction("OP_SETLOCAL", chunk, offset);
        case OP_GETLOCAL:
            return shortOperandInstruction("OP_GETLOCAL", chunk, offset);
        case OP_SETUPVAL:
            return shortOperandInstruction("OP_SETUPVAL", chunk, offset);
        case OP_GETUPVAL:
            return shortOperandInstruction("OP_GETUPVAL", chunk, offset);
        case OP_PEJMP:
            return longOperandInstruction("OP_PEJMP", chunk, offset);
        case OP_EJMP:
            return longOperandInstruction("OP_EJMP", chunk, offset);
        case OP_JMP:
            return longOperandInstruction("OP_JMP", chunk, offset);
        case OP_JMPBACK:
            return longOperandInstruction("OP_JMPBACK", chunk, offset);
        case OP_POP:
            return shortOperandInstruction("OP_POP", chunk, offset);
        case OP_CALL:
            return shortOperandInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            int index = readu16Chunk(chunk, offset + 1);
            printf("%-16s [%05d] - ", "OP_CLOSURE", index);
            CValue val = chunk->constants.values[index];
            CObjFunction *cobjFunc = (CObjFunction*)val.val.obj;
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
        case OP_NEWOBJECT:
            return shortOperandInstruction("OP_NEWOBJECT", chunk, offset);
        case OP_GETOBJECT:
            return simpleInstruction("OP_GETOBJECT", offset);
        case OP_SETOBJECT:
            return simpleInstruction("OP_SETOBJECT", offset);
        case OP_INVOKE:
            return shortOperandInstruction("OP_INVOKE", chunk, offset);
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
        case OP_CONCAT:
            return shortOperandInstruction("OP_CONCAT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode! [%d]\n", i);
            exit(0);
    }


    return 1;
}