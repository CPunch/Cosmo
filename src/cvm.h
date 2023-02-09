#ifndef COSMOVM_H
#define COSMOVM_H

#include "cosmo.h"
#include "cstate.h"

#include <string.h>

// #define VM_DEBUG

typedef enum
{
    COSMOVM_OK,
    COSMOVM_RUNTIME_ERR,
    COSMOVM_BUILDTIME_ERR
} COSMOVMRESULT;

// args = # of pass parameters, nresults = # of expected results
COSMO_API COSMOVMRESULT cosmoV_call(CState *state, int args, int nresults);
COSMO_API COSMOVMRESULT cosmoV_pcall(CState *state, int args, int nresults);

// pushes new object onto the stack & returns a pointer to the new object
COSMO_API CObjObject *cosmoV_makeObject(CState *state, int pairs);
COSMO_API void cosmoV_makeTable(CState *state, int pairs);
COSMO_API void cosmoV_concat(CState *state, int vals);
COSMO_API void cosmoV_pushFString(CState *state, const char *format, ...);
COSMO_API void cosmoV_printError(CState *state, CObjError *err);
COSMO_API CObjError *cosmoV_throw(CState *state);
COSMO_API void cosmoV_error(CState *state, const char *format, ...);
COSMO_API void cosmo_insert(CState *state, int indx, CValue val);

/*
    Sets the default proto objects for the passed objType. Also walks through the object heap and
   updates protos for the passed objType if that CObj* has no proto.

    returns true if replacing a previously registered proto object for this type
*/
COSMO_API bool cosmoV_registerProtoObject(CState *state, CObjType objType, CObjObject *obj);

/*
    compiles string into a <closure>, if successful, <closure> will be pushed onto the stack
   otherwise the <error> will be pushed.

    returns:
        false : <error> is at the top of the stack
        true  : <closure> is at the top of the stack
*/
COSMO_API bool cosmoV_compileString(CState *state, const char *src, const char *name);

/*
    expects object to be pushed, then the key.

    returns false if an error was thrown, returns true if the value was pushed onto the stack and
   the object and key were popped
*/
COSMO_API bool cosmoV_get(CState *state);

/*
    expects object to be pushed, then the key, and finally the new value.

    returns false if an error was thrown, returns true if the value was set and the object key, and
   value were popped
*/
COSMO_API bool cosmoV_set(CState *state);

// wraps the closure into a CObjMethod, so the function is called as an invoked method
COSMO_API bool cosmoV_getMethod(CState *state, CObj *obj, CValue key, CValue *val);

// clears the stack, callstack and restores the state into a usable state after a calloverflow or
// another hard to recover error (keeps the global table intact)
COSMO_API bool cosmoV_restore(CState *state);

// nice to have wrappers

// pushes a raw CValue to the stack, might throw an error if the stack is overflowed (with the
// SAFE_STACK macro on)
static inline void cosmoV_pushValue(CState *state, CValue val)
{
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
static inline StkPtr cosmoV_setTop(CState *state, int indx)
{
    state->top -= indx;
    return state->top;
}

// returns stack->top - indx - 1
static inline StkPtr cosmoV_getTop(CState *state, int indx)
{
    return &state->top[-(indx + 1)];
}

// pops 1 value off the stack, returns the popped value
static inline StkPtr cosmoV_pop(CState *state)
{
    return cosmoV_setTop(state, 1);
}

// pushes a cosmo_Number to the stack
static inline void cosmoV_pushNumber(CState *state, cosmo_Number num)
{
    cosmoV_pushValue(state, cosmoV_newNumber(num));
}

// pushes a boolean to the stack
static inline void cosmoV_pushBoolean(CState *state, bool b)
{
    cosmoV_pushValue(state, cosmoV_newBoolean(b));
}

static inline void cosmoV_pushRef(CState *state, CObj *obj)
{
    cosmoV_pushValue(state, cosmoV_newRef(obj));
}

// pushes a C Function to the stack
static inline void cosmoV_pushCFunction(CState *state, CosmoCFunction func)
{
    cosmoV_pushRef(state, (CObj *)cosmoO_newCFunction(state, func));
}

// len is the length of the string without the NULL terminator
static inline void cosmoV_pushLString(CState *state, const char *str, size_t len)
{
    cosmoV_pushRef(state, (CObj *)cosmoO_copyString(state, str, len));
}

// accepts a null terminated string and copys the buffer to the VM heap
static inline void cosmoV_pushString(CState *state, const char *str)
{
    cosmoV_pushLString(state, str, strlen(str));
}

static inline void cosmoV_pushNil(CState *state)
{
    cosmoV_pushValue(state, cosmoV_newNil());
}

#endif
