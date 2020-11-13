#ifndef COPERATORS_H
#define COPERATORS_H

#include "cosmo.h"

// instruction types
typedef enum {
    I_O, // just the operand (uint8_t)
    I_OBYTE, // operand (uint8_t) + uint8_t
    I_OSHORT, // operand (uint8_t) + uint16_t
} InstructionType;

// instructions 

typedef enum {
    // STACK MANIPULATION
    OP_LOADCONST,
    OP_SETGLOBAL, 
    OP_GETGLOBAL,
    OP_SETLOCAL,
    OP_GETLOCAL,
    OP_GETUPVAL,
    OP_SETUPVAL,
    OP_PEJMP, // pops, if false jumps uint16_t
    OP_EJMP, // if peek(0) is falsey jumps uint16_t
    OP_JMP, // always jumps uint16_t
    OP_JMPBACK, // jumps -uint16_t
    OP_POP, // - pops[uint8_t] from stack
    OP_CALL, // calls top[-uint8_t]
    OP_CLOSURE,
    OP_CLOSE,
    OP_NEWOBJECT,
    OP_GETOBJECT,
    OP_SETOBJECT,
    OP_INVOKE,

    // ARITHMETIC
    OP_ADD, 
    OP_SUB,
    OP_MULT,
    OP_DIV,
    OP_NOT,
    OP_NEGATE,
    OP_CONCAT, // concats uint8_t vars on the stack

    // EQUALITY
    OP_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_LESS_EQUAL,
    OP_GREATER_EQUAL,

    // LITERALS
    OP_TRUE,
    OP_FALSE,
    OP_NIL,

    OP_RETURN,

    OP_NONE // used as an error result
} COPCODE;

#endif