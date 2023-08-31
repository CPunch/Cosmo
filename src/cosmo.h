#ifndef COSMOMAIN_H
#define COSMOMAIN_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
    SAFE_STACK:
        if undefined, the stack will not be checked for stack overflows. This may improve
   performance, however this will produce undefined behavior as you reach the stack limit (and may
   cause a seg fault!). It is recommended to keep this enabled.
*/
// #define SAFE_STACK

/*
    NAN_BOXXED:
        if undefined, the interpreter will use a tagged union to store values. This is the default.
    Note that even though the sizeof(CValue) is 8 bytes for NAN_BOXXED (as opposed to 16 bytes for
   the tagged union) no performance benefits were measured. I recommend keeping this undefined for
   now.
*/
// #define NAN_BOXXED

// forward declare *most* stuff so our headers are cleaner
typedef struct CState CState;
typedef struct CChunk CChunk;
typedef struct CCallFrame CCallFrame;

#ifdef NAN_BOXXED
typedef union CValue CValue;
#else
typedef struct CValue CValue;
#endif

typedef struct CValueArray CValueArray;
typedef uint32_t cosmo_Flag;

// objs
typedef struct CObj CObj;
typedef struct CObjObject CObjObject;
typedef struct CObjString CObjString;
typedef struct CObjUpval CObjUpval;
typedef struct CObjFunction CObjFunction;
typedef struct CObjCFunction CObjCFunction;
typedef struct CObjMethod CObjMethod;
typedef struct CObjError CObjError;
typedef struct CObjTable CObjTable;
typedef struct CObjClosure CObjClosure;

typedef uint8_t INSTRUCTION;

typedef int (*cosmo_Reader)(CState *state, void *data, size_t size, const void *ud);
typedef int (*cosmo_Writer)(CState *state, const void *data, size_t size, const void *ud);

#define COSMOMAX_UPVALS 80
#define FRAME_MAX       64
#define STACK_MAX       (256 * FRAME_MAX)

#define COSMO_API       extern
#define UNNAMEDCHUNK    "_main"
#define COSMOASSERT(x)  assert(x)

#endif
