#ifndef COSMOVM_H
#define COSMOVM_H

#include <string.h>

#include "cosmo.h"
#include "cstate.h"

typedef enum {
    COSMOVM_OK,
    COSMOVM_RUNTIME_ERR,
    COSMOVM_BUILDTIME_ERR
} COSMOVMRESULT;

// args = # of pass parameters, nresults = # of expected results
COSMO_API COSMOVMRESULT cosmoV_call(CState *state, int args);
COSMO_API void cosmoV_pushObject(CState *state, int pairs);
COSMO_API bool cosmoV_getObject(CState *state, CObjObject *object, CValue key, CValue *val);

// nice to have wrappers

// pushes a cosmo_Number to the stack
static inline void cosmoV_pushNumber(CState *state, cosmo_Number num) {
    cosmoV_pushValue(state, cosmoV_newNumber(num));
}

// pushes a boolean to the stack
static inline void cosmoV_pushBoolean(CState *state, bool b) {
    cosmoV_pushValue(state, cosmoV_newBoolean(b));
}

// pushes a C Function to the stack
static inline void cosmoV_pushCFunction(CState *state, CosmoCFunction func) {
    cosmoV_pushValue(state, cosmoV_newObj(cosmoO_newCFunction(state, func)));
}

static inline void cosmoV_pushLString(CState *state, const char *str, size_t len) {
    cosmoV_pushValue(state, cosmoV_newObj(cosmoO_copyString(state, str, len)));
}

// accepts a null terminated string and copys the buffer to the VM heap
static inline void cosmoV_pushString(CState *state, const char *str) {
    cosmoV_pushLString(state, str, strlen(str));
}

#endif