#include "cdebug.h"

#include "cobj.h"
#include "cvalue.h"

void printIndent(int indent)
{
    for (int i = 0; i < indent; i++) {
        printf("\t");
    }
}

static int simpleInstruction(const char *name, int offset)
{
    printf("%s", name);
    return offset + 1; // consume opcode
}

static int u8OperandInstruction(const char *name, CChunk *chunk, int offset)
{
    printf("%-16s [%03d]", name, readu8Chunk(chunk, offset + 1));
    return offset + 2;
}

static int u16OperandInstruction(const char *name, CChunk *chunk, int offset)
{
    printf("%-16s [%05d]", name, readu16Chunk(chunk, offset + 1));
    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION));
}

static int JumpInstruction(const char *name, CChunk *chunk, int offset, int dir)
{
    int jmp = ((int)readu16Chunk(chunk, offset + 1)) * dir;
    printf("%-16s [%05d] - jumps to %04d", name, jmp, offset + 3 + jmp);
    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION));
}

static int u8u8OperandInstruction(const char *name, CChunk *chunk, int offset)
{
    printf("%-16s [%03d] [%03d]", name, readu8Chunk(chunk, offset + 1),
           readu8Chunk(chunk, offset + 2));
    return offset + 3; // op + u8 + u8
}

static int u8u16OperandInstruction(const char *name, CChunk *chunk, int offset)
{
    printf("%-16s [%03d] [%05d]", name, readu8Chunk(chunk, offset + 1),
           readu16Chunk(chunk, offset + 2));
    return offset + 4; // op + u8 + u16
}

static int u8u8u16OperandInstruction(const char *name, CChunk *chunk, int offset)
{
    printf("%-16s [%03d] [%03d] [%05d]", name, readu8Chunk(chunk, offset + 1),
           readu8Chunk(chunk, offset + 2), readu16Chunk(chunk, offset + 3));
    return offset + 5; // op + u8 + u8 + u16
}

static int constInstruction(const char *name, CChunk *chunk, int offset)
{
    int index = readu16Chunk(chunk, offset + 1);
    printf("%-16s [%05d] - ", name, index);
    CValue val = chunk->constants.values[index];

    cosmoV_printValue(val);

    return offset + 1 + (sizeof(uint16_t) / sizeof(INSTRUCTION)); // consume opcode + uint
}

// public methods in the cdebug.h header

void disasmChunk(CChunk *chunk, const char *name, int indent)
{
    printIndent(indent);
    printf("===[[ disasm for %s ]]===\n", name);

    for (size_t offset = 0; offset < chunk->count;) {
        offset = disasmInstr(chunk, offset, indent);
        printf("\n");
    }
}

int disasmInstr(CChunk *chunk, int offset, int indent)
{
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
        return constInstruction("OP_LOADCONST", chunk, offset);
    case OP_SETGLOBAL:
        return constInstruction("OP_SETGLOBAL", chunk, offset);
    case OP_GETGLOBAL:
        return constInstruction("OP_GETGLOBAL", chunk, offset);
    case OP_SETLOCAL:
        return u8OperandInstruction("OP_SETLOCAL", chunk, offset);
    case OP_GETLOCAL:
        return u8OperandInstruction("OP_GETLOCAL", chunk, offset);
    case OP_SETUPVAL:
        return u8OperandInstruction("OP_SETUPVAL", chunk, offset);
    case OP_GETUPVAL:
        return u8OperandInstruction("OP_GETUPVAL", chunk, offset);
    case OP_PEJMP:
        return JumpInstruction("OP_PEJMP", chunk, offset, 1);
    case OP_EJMP:
        return JumpInstruction("OP_EJMP", chunk, offset, 1);
    case OP_JMP:
        return JumpInstruction("OP_JMP", chunk, offset, 1);
    case OP_JMPBACK:
        return JumpInstruction("OP_JMPBACK", chunk, offset, -1);
    case OP_POP:
        return u8OperandInstruction("OP_POP", chunk, offset);
    case OP_CALL:
        return u8u8OperandInstruction("OP_CALL", chunk, offset);
    case OP_CLOSURE: {
        int index = readu16Chunk(chunk, offset + 1);
        printf("%-16s [%05d] - ", "OP_CLOSURE", index);
        CValue val = chunk->constants.values[index];
        CObjFunction *cobjFunc = (CObjFunction *)cosmoV_readRef(val);
        offset += 3; // we consumed the opcode + u16

        cosmoV_printValue(val);
        printf("\n");

        // list the upvalues/locals that are captured
        for (int i = 0; i < cobjFunc->upvals; i++) {
            uint8_t encoding = readu8Chunk(chunk, offset++);
            uint8_t index = readu8Chunk(chunk, offset++);
            printIndent(indent + 1);
            printf("references %s [%d]\n", encoding == OP_GETLOCAL ? "local" : "upvalue", index);
        }

        // print the chunk
        disasmChunk(&cobjFunc->chunk, cobjFunc->name == NULL ? UNNAMEDCHUNK : cobjFunc->name->str,
                    indent + 1);
        return offset;
    }
    case OP_CLOSE:
        return simpleInstruction("OP_CLOSE", offset);
    case OP_NEWTABLE:
        return u16OperandInstruction("OP_NEWTABLE", chunk, offset);
    case OP_NEWARRAY:
        return u16OperandInstruction("OP_NEWARRAY", chunk, offset);
    case OP_INDEX:
        return simpleInstruction("OP_INDEX", offset);
    case OP_NEWINDEX:
        return simpleInstruction("OP_NEWINDEX", offset);
    case OP_NEWOBJECT:
        return u16OperandInstruction("OP_NEWOBJECT", chunk, offset);
    case OP_SETOBJECT:
        return constInstruction("OP_SETOBJECT", chunk, offset);
    case OP_GETOBJECT:
        return constInstruction("OP_GETOBJECT", chunk, offset);
    case OP_GETMETHOD:
        return constInstruction("OP_GETMETHOD", chunk, offset);
    case OP_INVOKE:
        return u8u8u16OperandInstruction("OP_INVOKE", chunk, offset);
    case OP_ITER:
        return simpleInstruction("OP_ITER", offset);
    case OP_NEXT:
        return u8u16OperandInstruction("OP_NEXT", chunk, offset);
    case OP_ADD:
        return simpleInstruction("OP_ADD", offset);
    case OP_SUB:
        return simpleInstruction("OP_SUB", offset);
    case OP_MULT:
        return simpleInstruction("OP_MULT", offset);
    case OP_DIV:
        return simpleInstruction("OP_DIV", offset);
    case OP_MOD:
        return simpleInstruction("OP_MOD", offset);
    case OP_POW:
        return simpleInstruction("OP_POW", offset);
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
        return u8u8OperandInstruction("OP_INCUPVAL", chunk, offset);
    case OP_INCINDEX:
        return u8OperandInstruction("OP_INCINDEX", chunk, offset);
    case OP_INCOBJECT:
        return u8u16OperandInstruction("OP_INCOBJECT", chunk, offset);
    case OP_RETURN:
        return u8OperandInstruction("OP_RETURN", chunk, offset);
    default:
        printf("Unknown opcode! [%d]\n", i);
        return 1;
    }

    return 1;
}
