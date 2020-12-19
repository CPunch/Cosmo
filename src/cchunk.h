#ifndef CCHUNK_H
#define CCHUNK_H

#include "cosmo.h"

#include "coperators.h"
#include "cvalue.h"

typedef struct CValueArray CValueArray;

typedef struct CChunk {
    size_t capacity; // the amount of space we've allocated for
    size_t count; // the space we're currently using
    INSTRUCTION *buf; // whole chunk
    CValueArray constants; // holds constants
    size_t lineCapacity;
    int *lineInfo;
} CChunk;

CChunk *newChunk(CState* state, size_t startCapacity);
void initChunk(CState* state, CChunk *chunk, size_t startCapacity);
void cleanChunk(CState* state, CChunk *chunk); // frees everything but the struct
void freeChunk(CState* state, CChunk *chunk); // frees everything including the struct
int addConstant(CState* state, CChunk *chunk, CValue value);

// write to chunk
void writeu8Chunk(CState* state, CChunk *chunk, INSTRUCTION i, int line);
void writeu16Chunk(CState* state, CChunk *chunk, uint16_t i, int line);

// read from chunk
static inline INSTRUCTION readu8Chunk(CChunk *chunk, int offset) {
    return chunk->buf[offset];
}

static inline uint16_t readu16Chunk(CChunk *chunk, int offset) {
    return *((uint16_t*)(&chunk->buf[offset]));
}

#endif