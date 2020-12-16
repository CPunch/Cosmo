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
    // STACK/STATE MANIPULATION
    OP_LOADCONST, // pushes const[uint8_t] to the stack
    OP_SETGLOBAL, // pops and sets global[const[uint16_t]]
    OP_GETGLOBAL, // pushes global[const[uint16_t]]
    OP_SETLOCAL, // pops and sets base[uint8_t]
    OP_GETLOCAL, // pushes base[uint8_t]
    OP_GETUPVAL, // pushes closure->upvals[uint8_t]
    OP_SETUPVAL, // pops and sets closure->upvals[uint8_t]
    OP_PEJMP, // pops, if false jumps uint16_t
    OP_EJMP, // if peek(0) is falsey jumps uint16_t
    OP_JMP, // always jumps uint16_t
    OP_JMPBACK, // jumps -uint16_t
    OP_POP, // - pops[uint8_t] from stack
    OP_CALL, // calls top[-uint8_t] expecting uint8_t results
    OP_CLOSURE, 
    OP_CLOSE,
    OP_NEWDICT,
    OP_INDEX,
    OP_NEWINDEX,
    OP_NEWOBJECT,
    OP_SETOBJECT,
    OP_GETOBJECT,
    OP_INVOKE,
    OP_ITER, // if stack[top] is an object, __iter is expected and called, else if stack[top] is a dictionary a dummy iterator object is made (SEE: cosmoV_makeIter())
    OP_NEXT, // if stack[top] is an object, __next is expected and called, expecting uint8_t return values. if stack[top] after calling is nil, jump uint16_t

    // ARITHMETIC
    OP_ADD,
    OP_SUB,
    OP_MULT,
    OP_DIV,
    OP_NOT,
    OP_NEGATE,
    OP_COUNT,
    OP_CONCAT, // concats uint8_t vars on the stack
    OP_INCLOCAL, // pushes old value to stack, adds (uint8_t-128) to local[uint8_t]
    OP_INCGLOBAL, // pushes old value to stack, adds (uint8_t-128) to globals[const[uint16_t]]
    OP_INCUPVAL, // pushes old value to stack, adds (uint8_t-128) to closure->upval[uint8_t]
    OP_INCINDEX, // pushes old value to stack, adds (uint8_t-128) to dict[pop()]
    OP_INCOBJECT, // pushes old value to stack, adds (uint8_t-128) to obj[const[uint16_t]]

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

    OP_RETURN
} COPCODE; // there can be a max of 256 instructions

#endif