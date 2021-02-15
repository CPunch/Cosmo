#include "cmem.h"
#include "cchunk.h"
#include "cvalue.h"
#include "cvm.h"
#include "cobj.h"

CChunk *newChunk(CState* state, size_t startCapacity) {
    CChunk *chunk = cosmoM_xmalloc(state, sizeof(CChunk));
    initChunk(state, chunk, startCapacity);
    return chunk;
}

void initChunk(CState* state, CChunk *chunk, size_t startCapacity) {
    chunk->capacity = startCapacity;
    chunk->lineCapacity = startCapacity;
    chunk->count = 0;
    chunk->buf = NULL; // when writeByteChunk is called, it'll allocate the array for us
    chunk->lineInfo = NULL;
    
    // constants
    initValArray(state, &chunk->constants, ARRAY_START);
}

void cleanChunk(CState* state, CChunk *chunk) {
    // first, free the chunk buffer
    cosmoM_freearray(state, INSTRUCTION, chunk->buf, chunk->capacity);
    // then the line info
    cosmoM_freearray(state, int, chunk->lineInfo, chunk->capacity);
    // free the constants
    cleanValArray(state, &chunk->constants);
}

void freeChunk(CState* state, CChunk *chunk) {
    cleanChunk(state, chunk);
    // now, free the wrapper struct
    cosmoM_free(state, CChunk, chunk);
}

int addConstant(CState* state, CChunk *chunk, CValue value) {
    // before adding the constant, check if we already have it
    for (size_t i = 0; i < chunk->constants.count; i++) {
        if (cosmoV_equal(value, chunk->constants.values[i]))
            return i; // we already have a matching constant!
    }

    cosmoM_freezeGC(state); // so our GC doesn't free it
    appendValArray(state, &chunk->constants, value);
    cosmoM_unfreezeGC(state);
    return chunk->constants.count - 1; // return the index of the new constants
}

// ================================================================ [WRITE TO CHUNK] ================================================================

void writeu8Chunk(CState* state, CChunk *chunk, INSTRUCTION i, int line) {
    // does the buffer need to be reallocated?
    cosmoM_growarray(state, INSTRUCTION, chunk->buf, chunk->count, chunk->capacity);
    cosmoM_growarray(state, int, chunk->lineInfo, chunk->count, chunk->lineCapacity);

    // write data to the chunk :)
    chunk->lineInfo[chunk->count] = line;
    chunk->buf[chunk->count++] = i;
}

void writeu16Chunk(CState* state, CChunk *chunk, uint16_t i, int line) {
    INSTRUCTION *buffer = (INSTRUCTION*)(&i);
    int sz = sizeof(uint16_t) / sizeof(INSTRUCTION);

    for (int i = 0; i < sz; i++) {
        writeu8Chunk(state, chunk, buffer[i], line);
    }
}
