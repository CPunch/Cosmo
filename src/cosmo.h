#ifndef COSMOMAIN_H
#define COSMOMAIN_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// forward declare *most* stuff so our headers are cleaner
typedef struct CState CState;
typedef struct CChunk CChunk;
typedef struct CValue CValue;

// objs
typedef struct CObj CObj;
typedef struct CObjString CObjString;
typedef struct CObjUpval CObjUpval;
typedef struct CObjFunction CObjFunction;
typedef struct CObjCFunction CObjCFunction;
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