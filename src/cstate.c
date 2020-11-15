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
    state->freezeGC = false;

    // GC
    state->objects = NULL;
    state->grayCount = 0;
    state->grayCapacity = 2;
    state->grayStack = NULL;
    state->allocatedBytes = 0;
    state->nextGC = 1024 * 8; // threshhold starts at 8kb

    // init stack
    state->top = state->stack;
    state->frameCount = 0;
    state->openUpvalues = NULL;
    state->protoObj = NULL;

    cosmoT_initTable(state, &state->strings, 8); // init string table
    cosmoT_initTable(state, &state->globals, 8); // init global table

    // first, set all strings to NULL so our GC doesn't read garbage data
    for (int i = 0; i < INTERNALSTRING_MAX; i++)
        state->internalStrings[i] = NULL;

    // setup all strings used by the VM
    state->internalStrings[INTERNALSTRING_INIT] = cosmoO_copyString(state, "__init", 6);
    return state;
}

void cosmoV_freeState(CState *state) {
#ifdef GC_DEBUG
    printf("state %p is being free'd!\n", state);
#endif
    // frees all the objects
    CObj *objs = state->objects;
    while (objs != NULL) {
        CObj *next = objs->next;
        cosmoO_free(state, objs);
        objs = next;
    }

    // mark our internal VM strings NULL
    for (int i = 0; i < INTERNALSTRING_MAX; i++)
        state->internalStrings[i] = NULL;

    // free our string & global table (the string table includes the internal VM strings)
    cosmoT_clearTable(state, &state->strings);
    cosmoT_clearTable(state, &state->globals);
    
    // free our gray stack & finally free the state structure
    free(state->grayStack);
    free(state);
}

void cosmoV_register(CState *state, const char *identifier, CValue val) {
    // we push the values so the garbage collector can find them
    cosmoV_pushValue(state, val);
    cosmoV_pushValue(state, cosmoV_newObj(cosmoO_copyString(state, identifier, strlen(identifier))));

    CValue *oldVal = cosmoT_insert(state, &state->globals, *cosmoV_getTop(state, 0));
    *oldVal = val;
    
    cosmoV_setTop(state, 2); // pops the 2 values off the stack
}

void cosmoV_printStack(CState *state) {
    printf("==== [[ stack dump ]] ====\n");
    for (CValue *top = state->top - 1; top >= state->stack; top--) {
        printf("%d: ", (int)(top - state->stack));
        printValue(*top);
        printf("\n");
    }
}