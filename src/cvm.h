#ifndef COSMOVM_H
#define COSMOVM_H

#include <string.h>

#include "cosmo.h"
#include "cstate.h"

/* 
    SAFE_STACK:
        if undefined, the stack will not be checked for stack overflows. This may improve performance, however 
    this will produce undefined behavior as you reach the stack limit (and may cause a seg fault!). It is recommended to keep this enabled.
*/
#define SAFE_STACK

typedef enum {
    COSMOVM_OK,
    COSMOVM_RUNTIME_ERR,
    COSMOVM_BUILDTIME_ERR
} COSMOVMRESULT;

// args = # of pass parameters, nresults = # of expected results
COSMO_API COSMOVMRESULT cosmoV_call(CState *state, int args, int nresults);
COSMO_API void cosmoV_makeObject(CState *state, int pairs);
COSMO_API void cosmoV_makeDictionary(CState *state, int pairs);
COSMO_API bool cosmoV_getObject(CState *state, CObjObject *object, CValue key, CValue *val);
COSMO_API void cosmoV_concat(CState *state, int vals);
COSMO_API void cosmoV_pushFString(CState *state, const char *format, ...);
COSMO_API void cosmoV_error(CState *state, const char *format, ...);

// nice to have wrappers

// pushes value to the stack
static inline void cosmoV_pushValue(CState *state, CValue val) {
#ifdef SAFE_STACK
    ptrdiff_t stackSize = state->top - state->stack;

    // we reserve 8 slots for the error string and whatever c api we might be in
    if (stackSize >= STACK_MAX - 8) {
        if (state->panic) { // we're in a panic state, let the 8 reserved slots be filled
            if (stackSize < STACK_MAX)
                *(state->top++) = val;
            
            return;
        }

        cosmoV_error(state, "Stack overflow!");
        return;
    }
#endif

    *(state->top++) = val;
}

// sets stack->top to stack->top - indx
static inline StkPtr cosmoV_setTop(CState *state, int indx) {
    state->top -= indx;
    return state->top;
}

// returns stack->top - indx - 1
static inline StkPtr cosmoV_getTop(CState *state, int indx) {
    return &state->top[-(indx + 1)];
}

// pops 1 value off the stack
static inline StkPtr cosmoV_pop(CState *state) {
    return cosmoV_setTop(state, 1);
}

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
