#ifndef CSTATE_H
#define CSTATE_H

#include "cobj.h"
#include "cosmo.h"
#include "ctable.h"
#include "cvalue.h"

#include <setjmp.h>

struct CCallFrame
{
    CObjClosure *closure;
    INSTRUCTION *pc;
    CValue *base;
};

typedef enum IStringEnum
{
    ISTRING_INIT,     // __init
    ISTRING_GC,       // __gc
    ISTRING_TOSTRING, // __tostring
    ISTRING_TONUMBER, // __tonumber
    ISTRING_EQUAL,    // __equals
    ISTRING_INDEX,    // __index
    ISTRING_NEWINDEX, // __newindex
    ISTRING_COUNT,    // __count
    ISTRING_GETTER,   // __getter
    ISTRING_SETTER,   // __setter
    ISTRING_ITER,     // __iter
    ISTRING_NEXT,     // __next
    ISTRING_RESERVED, // __reserved
    ISTRING_MAX // if this becomes greater than 33, we are out of space in cosmo_Flag. you'll have
                // to change that to uint64_t
} IStringEnum;

typedef struct ArrayCObj
{
    CObj **array;
    int count;
    int capacity;
} ArrayCObj;

typedef struct CPanic
{
    jmp_buf jmp;
    StkPtr top;
    struct CPanic *prev;
    int frameCount;
    int freezeGC;
} CPanic;

struct CState
{
    CCallFrame callFrame[FRAME_MAX];    // call frames
    CValue stack[STACK_MAX];            // stack
    CObjObject *protoObjects[COBJ_MAX]; // proto object for each COBJ type [NULL = no default proto]
    CObjString *iStrings[ISTRING_MAX];  // strings used internally by the VM, eg. __init, __index
    CTable strings;
    CTable registry;
    ArrayCObj grayStack; // keeps track of which objects *haven't yet* been traversed in our GC, but
                         // *have been* found

    CObjUpval *openUpvalues; // tracks all of our still open (meaning still on the stack) upvalues
    CObjTable *globals;
    CValue *top;     // top of the stack
    CObj *objects;   // tracks all of our allocated objects
    CPanic *panic;

    size_t allocatedBytes;
    size_t nextGC; // when allocatedBytes reaches this threshhold, trigger a GC event
    int freezeGC; // when > 0, GC events will be ignored (for internal use)
    int frameCount;
};

CPanic *cosmoV_newPanic(CState *state);
void cosmoV_freePanic(CState *state);

COSMO_API CState *cosmoV_newState();
COSMO_API void cosmoV_freeState(CState *state);

// expects 2*pairs values on the stack, each pair should consist of 1 key and 1 value
COSMO_API void cosmoV_addGlobals(CState *state, int pairs);

// expects 2*pairs values on the stack, each pair should consist of 1 key and 1 value
COSMO_API void cosmoV_addRegistry(CState *state, int pairs);

// expects 1 key on the stack, pushes result
COSMO_API void cosmoV_getRegistry(CState *state);

// expects <object>->proto = <object> (2 total) to be on the stack
COSMO_API void cosmoV_setProto(CState *state);

COSMO_API void cosmoV_printStack(CState *state);

#endif
