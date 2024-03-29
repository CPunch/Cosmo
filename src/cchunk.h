#ifndef CCHUNK_H
#define CCHUNK_H

#include "coperators.h"
#include "cosmo.h"
#include "cvalue.h"

struct CChunk
{
    size_t capacity;       // the amount of space we've allocated for
    size_t count;          // the space we're currently using
    INSTRUCTION *buf;      // whole chunk
    CValueArray constants; // holds constants
    size_t lineCapacity;
    int *lineInfo;
};

CChunk *newChunk(CState *state, size_t startCapacity);
void initChunk(CState *state, CChunk *chunk, size_t startCapacity);
void cleanChunk(CState *state, CChunk *chunk); // frees everything but the struct
void freeChunk(CState *state, CChunk *chunk);  // frees everything including the struct
int addConstant(CState *state, CChunk *chunk, CValue value);

bool validateChunk(CState *state, CChunk *chunk);

// write to chunk
void writeu8Chunk(CState *state, CChunk *chunk, INSTRUCTION i, int line);
void writeu16Chunk(CState *state, CChunk *chunk, uint16_t i, int line);

// read from chunk
static inline INSTRUCTION readu8Chunk(CChunk *chunk, int offset)
{
    return chunk->buf[offset];
}

static inline uint16_t readu16Chunk(CChunk *chunk, int offset)
{
    return *((uint16_t *)(&chunk->buf[offset]));
}

#endif
