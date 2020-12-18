#ifndef CSTATE_H
#define CSTATE_H

#include "cosmo.h"
#include "cvalue.h"
#include "cobj.h"
#include "ctable.h"

typedef struct CCallFrame {
    CObjClosure *closure;
    INSTRUCTION *pc;
    CValue* base;
} CCallFrame;

typedef enum IStringEnum {
    ISTRING_INIT,       // __init
    ISTRING_INDEX,      // __index
    ISTRING_NEWINDEX,   // __newindex
    ISTRING_GETTER,     // __getter
    ISTRING_SETTER,     // __setter
    ISTRING_ITER,       // __iter
    ISTRING_NEXT,       // __next
    ISTRING_RESERVED,   // __reserved
    ISTRING_MAX
} IStringEnum;

typedef struct ArrayCObj {
    CObj **array;
    int count;
    int capacity;
} ArrayCObj;

typedef struct CState {
    bool panic;
    int freezeGC; // when > 0, GC events will be ignored (for internal use)
    CObj *objects; // tracks all of our allocated objects
    CObj *userRoots; // user definable roots, this holds CObjs that should be considered "roots", lets the VM know you are holding a reference to a CObj in your code
    ArrayCObj grayStack; // keeps track of which objects *haven't yet* been traversed in our GC, but *have been* found
    size_t allocatedBytes;
    size_t nextGC; // when allocatedBytes reaches this threshhold, trigger a GC event

    CObjUpval *openUpvalues; // tracks all of our still open (meaning still on the stack) upvalues
    CTable strings;
    CTable globals;

    CValue *top; // top of the stack
    CValue stack[STACK_MAX]; // stack
    CCallFrame callFrame[FRAME_MAX]; // call frames
    int frameCount;

    CObjString *iStrings[ISTRING_MAX]; // strings used internally by the VM, eg. __init, __index & friends
    CObjObject *protoObj; // start met obj for all objects (NULL by default)
} CState;

COSMO_API CState *cosmoV_newState();
COSMO_API void cosmoV_register(CState *state, const char *identifier, CValue val);
COSMO_API void cosmoV_freeState(CState *state);
COSMO_API void cosmoV_printStack(CState *state);

// pushes value to the stack
static inline void cosmoV_pushValue(CState *state, CValue val) {
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

#endif