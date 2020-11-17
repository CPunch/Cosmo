#include "cmem.h"
#include "cstate.h"
#include "cvalue.h"
#include "ctable.h"
#include "cparse.h"
#include "cobj.h"
#include "cbaselib.h"

/*
    copy buffer to new larger buffer, and free the old buffer
*/
void *cosmoM_reallocate(CState* state, void *buf, size_t oldSize, size_t newSize) {
    state->allocatedBytes += newSize - oldSize;

    if (newSize == 0) { // it needs to be free'd
        free(buf);
        return NULL;
    }

#ifdef GC_STRESS
    if (!(cosmoM_isFrozen(state)) && newSize > oldSize) {
        cosmoM_collectGarbage(state);
    }
#ifdef GC_DEBUG
    else {
        printf("GC event ignored! state frozen! [%d]\n", state->freezeGC);
    }
#endif
#else
    // if the state isn't frozen && we've reached the GC event
    if (!(cosmoM_isFrozen(state)) && state->allocatedBytes > state->nextGC) {
        cosmoM_collectGarbage(state); // cya lol
    }
#endif

    // otherwise just use realloc to do all the heavy lifting
    void *newBuf = realloc(buf, newSize);

    if (newBuf == NULL) {
        CERROR("failed to allocate memory!");
        exit(1);
    }

    return newBuf;
}

void markObject(CState *state, CObj *obj);
void markValue(CState *state, CValue val);

void markTable(CState *state, CTable *tbl) {
    if (tbl->table == NULL) // table is still being initialized
        return;
    
    for (int i = 0; i < tbl->capacity; i++) {
        CTableEntry *entry = &tbl->table[i];
        markValue(state, entry->key);
        markValue(state, entry->val);
    }
}

// free's white members from the table
void tableRemoveWhite(CState *state, CTable *tbl) {
    if (tbl->table == NULL) // table is still being initialized
        return;
    
    for (int i = 0; i < tbl->capacity; i++) {
        CTableEntry *entry = &tbl->table[i];
        if (IS_OBJ(entry->key) && !(entry->key.val.obj)->isMarked) { // if the key is a object and it's white (unmarked), remove it from the table
            cosmoT_remove(tbl, entry->key);
        }
    }
}

void markArray(CState *state, CValueArray *array) {
    for (int i = 0; i < array->count; i++) {
        markValue(state, array->values[i]);
    }
}

// mark all references associated with the object
void blackenObject(CState *state, CObj *obj) {
    switch (obj->type) {
        case COBJ_STRING:
        case COBJ_CFUNCTION:
            // stubbed
            break;
        case COBJ_OBJECT: {
            // mark everything this object is keeping track of
            CObjObject *cobj = (CObjObject*)obj;
            markTable(state, &cobj->tbl);
            markObject(state, (CObj*)cobj->proto);
            break;
        }
        case COBJ_UPVALUE: {
            markValue(state, ((CObjUpval*)obj)->closed);
            break;
        }
        case COBJ_FUNCTION: {
            CObjFunction *func = (CObjFunction*)obj;
            markObject(state, (CObj*)func->name);
            markArray(state, &func->chunk.constants);

            break;
        }
        case COBJ_METHOD: {
            CObjMethod *method = (CObjMethod*)obj;
            markObject(state, (CObj*)method->closure);
            markObject(state, (CObj*)method->obj);
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)obj;
            markObject(state, (CObj*)closure->function);

            // mark all upvalues
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject(state, (CObj*)closure->upvalues[i]);
            }

            break;
        }
        default:
            printf("Unknown type in blackenObject with %p, type %d\n", obj, obj->type);
            break;
    }
}

void markObject(CState *state, CObj *obj) {
    if (obj == NULL || obj->isMarked) // skip if NULL or already marked
        return;

    obj->isMarked = true;

#ifdef GC_DEBUG
    printf("marking %p, [", obj);
    printObject(obj);
    printf("]\n");
#endif

    // they don't need to be added to the gray stack, they don't reference any other CObjs
    if (obj->type == COBJ_CFUNCTION || obj->type == COBJ_STRING) 
        return;

    // we don't use cosmoM_growarray because we don't want to trigger another GC event while in the GC!
    if (state->grayCount >= state->grayCapacity || state->grayStack == NULL) { 
        int old = state->grayCapacity; 
        state->grayCapacity = old * GROW_FACTOR; 
        state->grayStack = (CObj**)realloc(state->grayStack, sizeof(CObj*) * state->grayCapacity); 

        if (state->grayStack == NULL) {
            CERROR("failed to allocate memory for grayStack!");
            exit(1);
        }
    }

    state->grayStack[state->grayCount++] = obj;
}

void markValue(CState *state, CValue val) {
    if (IS_OBJ(val))
        markObject(state, cosmoV_readObj(val));
}

// trace our gray references
void traceGrays(CState *state) {
    while (state->grayCount > 0) {
        CObj* obj = state->grayStack[--state->grayCount];
        blackenObject(state, obj);
    }
}

void sweep(CState *state) {
    CObj *prev = NULL;
    CObj *object = state->objects;
    while (object != NULL) {
        if (object->isMarked) { // skip over it
            object->isMarked = false; // rest to white
            prev = object;
            object = object->next;
        } else { // free it!
            CObj *oldObj = object;

            object = object->next;
            if (prev == NULL) {
                state->objects = object;
            } else {
                prev->next = object;
            }

            cosmoO_free(state, oldObj);
        }
    }
}

void markRoots(CState *state) {
    // mark all values on the stack
    for (StkPtr value = state->stack; value < state->top; value++) {
        markValue(state, *value);
    }

    // mark all active callframe closures
    for (int i = 0; i < state->frameCount; i++) {
        markObject(state, (CObj*)state->callFrame[i].closure);
    }

    // mark all open upvalues
    for (CObjUpval *upvalue = state->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject(state, (CObj*)upvalue);
    }

    markTable(state, &state->globals);

    // mark all internal strings
    for (int i = 0; i < ISTRING_MAX; i++)
        markObject(state, (CObj*)state->iStrings[i]);

    // mark our proto object
    markObject(state, (CObj*)state->protoObj);
    traceGrays(state);
}

COSMO_API void cosmoM_collectGarbage(CState *state) {
#ifdef GC_DEBUG
    printf("-- GC start\n");
    size_t start = state->allocatedBytes;
#endif

    markRoots(state);

    tableRemoveWhite(state, &state->strings); // make sure we aren't referencing any strings that are about to be free'd
    // now finally, free all the unmarked objects
    sweep(state);

    // set our next GC event
    state->nextGC = state->allocatedBytes * HEAP_GROW_FACTOR;

#ifdef GC_DEBUG
    printf("-- GC end, reclaimed %ld bytes (started at %ld, ended at %ld), next garbage collection scheduled at %ld bytes\n",
            start - state->allocatedBytes, start, state->allocatedBytes, state->nextGC);
#endif
}