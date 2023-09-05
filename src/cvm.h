#ifndef COSMOVM_H
#define COSMOVM_H

#include "cosmo.h"
#include "cstate.h"

#include <string.h>

// #define VM_DEBUG

/*
    if we're using GNUC or clang, we can use computed gotos which speeds up
    cosmoV_execute by about 20% from benchmarking. of course, if you know
    your compiler supports computed gotos, you can define VM_JUMPTABLE

    although, this is disabled when VM_DEBUG is defined, since it can cause
    issues with debugging

    BTW: be weary of maliciously crafted cosmo dumps!! it's very easy to crash
    cosmo with this enabled and reading invalid opcodes due to us just using the
    opcode as an index into the jump table
*/
#if (defined(__GNUC__) || defined(__clang__)) && !defined(VM_DEBUG)
#    define VM_JUMPTABLE
#endif

// args = # of pass parameters, nresults = # of expected results
COSMO_API void cosmoV_call(CState *state, int args, int nresults);
COSMO_API bool cosmoV_pcall(CState *state, int args, int nresults);

// pushes new object onto the stack & returns a pointer to the new object
COSMO_API CObjObject *cosmoV_makeObject(CState *state, int pairs);
COSMO_API void cosmoV_makeTable(CState *state, int pairs);
COSMO_API void cosmoV_concat(CState *state, int vals);
COSMO_API void cosmoV_pushFString(CState *state, const char *format, ...);
COSMO_API void cosmoV_printError(CState *state, CObjError *err);
COSMO_API void cosmoV_throw(CState *state);
COSMO_API void cosmoV_error(CState *state, const char *format, ...);
COSMO_API void cosmoV_insert(CState *state, int indx, CValue val);

/*
    Sets the default proto objects for the passed objType. Also walks through the object heap and
    updates protos for the passed objType if that CObj* has no proto.

    returns true if replacing a previously registered proto object for this type
*/
COSMO_API bool cosmoV_registerProtoObject(CState *state, CObjType objType, CObjObject *obj);

/*
    compiles string into a <closure>. if successful, <closure> will be pushed onto the stack
   otherwise the <error> will be pushed.

    returns:
        false : <error> is at the top of the stack
        true  : <closure> is at the top of the stack
*/
COSMO_API bool cosmoV_compileString(CState *state, const char *src, const char *name);

/*
    loads a <closure> from a dump. if successful, <closure> will be pushed onto the stack
   otherwise the <error> will be pushed.

   returns:
        false : <error> is at the top of the stack
        true  : <closure> is at the top of the stack
*/
COSMO_API bool cosmoV_undump(CState *state, cosmo_Reader reader, const void *ud);

/*
    expects object to be pushed, then the key. pops the key & object and pushes the value
*/
COSMO_API void cosmoV_get(CState *state);

/*
    expects object to be pushed, then the key, and finally the new value. pops the object, key & value
*/
COSMO_API void cosmoV_set(CState *state);

// wraps the closure into a CObjMethod, so the function is called as an invoked method
COSMO_API void cosmoV_getMethod(CState *state, CObj *obj, CValue key, CValue *val);

// check if the value at the top of the stack is a <obj> user type
COSMO_API bool cosmoV_isValueUserType(CState *state, CValue val, int userType);

// nice to have wrappers

// pushes a raw CValue to the stack, might throw an error if the stack is overflowed (with the
// SAFE_STACK macro on)
static inline void cosmoV_pushValue(CState *state, CValue val)
{
#ifdef SAFE_STACK
    ptrdiff_t stackSize = state->top - state->stack;

    // we reserve 8 slots for the error string and whatever c api we might be in
    if (stackSize >= STACK_MAX - 8) {
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
