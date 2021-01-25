#include "cstate.h"
#include "cchunk.h"
#include "cobj.h"
#include "cvm.h"
#include "cmem.h"

#include <string.h>

CState *cosmoV_newState() {
    // we use C's malloc because we don't want to trigger a GC with an invalid state
    CState *state = malloc(sizeof(CState));

    if (state == NULL) {
        CERROR("failed to allocate memory!");
        exit(1);
    }

    state->panic = false;
    state->freezeGC = 1; // we start frozen

    // GC
    state->objects = NULL;
    state->userRoots = NULL;
    state->grayStack.count = 0;
    state->grayStack.capacity = 2;
    state->grayStack.array = NULL;
    state->allocatedBytes = sizeof(CState);
    state->nextGC = 1024 * 8; // threshhold starts at 8kb

    // init stack
    state->top = state->stack;
    state->frameCount = 0;
    state->openUpvalues = NULL;

    state->error = NULL;
    
    // set default proto objects
    for (int i = 0; i < COBJ_MAX; i++)
        state->protoObjects[i] = NULL;

    for (int i = 0; i < ISTRING_MAX; i++)
        state->iStrings[i] = NULL;

    cosmoT_initTable(state, &state->strings, 16); // init string table

    state->globals = cosmoO_newTable(state); // init global table

    // setup all strings used by the VM
    state->iStrings[ISTRING_INIT] = cosmoO_copyString(state, "__init", 6);
    state->iStrings[ISTRING_TOSTRING] = cosmoO_copyString(state, "__tostring", 10);
    state->iStrings[ISTRING_TONUMBER] = cosmoO_copyString(state, "__tonumber", 10);
    state->iStrings[ISTRING_INDEX] = cosmoO_copyString(state, "__index", 7);
    state->iStrings[ISTRING_NEWINDEX] = cosmoO_copyString(state, "__newindex", 10);
    state->iStrings[ISTRING_COUNT] = cosmoO_copyString(state, "__count", 7);

    // getters/setters
    state->iStrings[ISTRING_GETTER] = cosmoO_copyString(state, "__getter", 8);
    state->iStrings[ISTRING_SETTER] = cosmoO_copyString(state, "__setter", 8);

    // for iterators
    state->iStrings[ISTRING_ITER] = cosmoO_copyString(state, "__iter", 6);
    state->iStrings[ISTRING_NEXT] = cosmoO_copyString(state, "__next", 6);

    // for reserved members for objects
    state->iStrings[ISTRING_RESERVED] = cosmoO_copyString(state, "__reserved", 10);

    // set the IString flags
    for (int i = 0; i < ISTRING_MAX; i++)
        state->iStrings[i]->isIString = true;

    state->freezeGC = 0; // unfreeze the state
    return state;
}

void cosmoV_freeState(CState *state) {
#ifdef GC_DEBUG
    printf("state %p is being free'd!\n", state);
#endif
    cosmoM_freezeGC(state);

    // frees all the objects
    CObj *objs = state->objects;
    while (objs != NULL) {
        CObj *next = objs->next;
        cosmoO_free(state, objs);
        objs = next;
    }

    // mark our internal VM strings NULL
    for (int i = 0; i < ISTRING_MAX; i++)
        state->iStrings[i] = NULL;

    // free our string table (the string table includes the internal VM strings)
    cosmoT_clearTable(state, &state->strings);
    
    // free our gray stack & finally free the state structure
    cosmoM_freearray(state, CObj*, state->grayStack.array, state->grayStack.capacity);

    // TODO: yeah idk, it looks like im missing 520 bytes somewhere? i'll look into it later
/*#ifdef GC_DEBUG
    if (state->allocatedBytes != sizeof(CState)) {
        printf("state->allocatedBytes doesn't match expected value (%lu), got %lu!", sizeof(CState), state->allocatedBytes);
        exit(0);
    }
#endif*/
    free(state);
}

// expects 2*pairs values on the stack, each pair should consist of 1 key and 1 value
void cosmoV_register(CState *state, int pairs) {
    for (int i = 0; i < pairs; i++) {
        StkPtr key = cosmoV_getTop(state, 1);
        StkPtr val = cosmoV_getTop(state, 0);

        CValue *oldVal = cosmoT_insert(state, &state->globals->tbl, *key);
        *oldVal = *val;
        
        cosmoV_setTop(state, 2); // pops the 2 values off the stack
    }
}

void cosmoV_printStack(CState *state) {
    printf("==== [[ stack dump ]] ====\n");
    for (CValue *top = state->top - 1; top >= state->stack; top--) {
        printf("%d: ", (int)(top - state->stack));
        printValue(*top);
        printf("\n");
    }
}
