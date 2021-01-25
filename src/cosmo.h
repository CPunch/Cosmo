#ifndef COSMOMAIN_H
#define COSMOMAIN_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <assert.h>

/* 
    SAFE_STACK:
        if undefined, the stack will not be checked for stack overflows. This may improve performance, however 
    this will produce undefined behavior as you reach the stack limit (and may cause a seg fault!). It is recommended to keep this enabled.
*/
#define SAFE_STACK

//#define NAN_BOXXED

#define COSMOASSERT(x) assert(x)

// forward declare *most* stuff so our headers are cleaner
typedef struct CState CState;
typedef struct CChunk CChunk;
#ifdef NAN_BOXXED
typedef union CValue CValue;
#else
typedef struct CValue CValue;
#endif

// objs
typedef struct CObj CObj;
typedef struct CObjString CObjString;
typedef struct CObjUpval CObjUpval;
typedef struct CObjFunction CObjFunction;
typedef struct CObjCFunction CObjCFunction;
typedef struct CObjMethod CObjMethod;
typedef struct CObjError CObjError;
typedef struct CObjObject CObjObject;
typedef struct CObjTable CObjTable;
typedef struct CObjClosure CObjClosure;

typedef uint8_t INSTRUCTION;

#define COSMOMAX_UPVALS 80
#define FRAME_MAX   64
#define STACK_MAX   (256 * FRAME_MAX)

#define COSMO_API extern
#define UNNAMEDCHUNK "_main"

#define CERROR(err) \
    printf("%s : %s\n", "[ERROR]", err)

#endif
