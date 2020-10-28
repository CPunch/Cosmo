#ifndef CPARSE_H
#define CPARSE_H

#include "cosmo.h"
#include "clex.h"

typedef struct {
    CToken name;
    int depth;
    bool isCaptured; // is the Local referenced in an upvalue?
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    FTYPE_FUNCTION,
    FTYPE_SCRIPT
} FunctionType;

typedef struct CCompilerState {
    CObjFunction *function;
    FunctionType type;

    Local locals[256];
    Upvalue upvalues[256];
    int localCount;
    int scopeDepth;
    int pushedValues;
    int savedPushed;
    struct CCompilerState* enclosing;
} CCompilerState;

// compiles source into CChunk, if NULL is returned, a syntaxical error has occured and pushed onto the stack
CObjFunction* cosmoP_compileString(CState *state, const char *source);

#endif