#include "cmem.h"
#include "cstate.h"
#include "cvalue.h"
#include "ctable.h"
#include "cparse.h"
#include "cobj.h"
#include "cbaselib.h"

// realloc wrapper
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
    cosmoM_checkGarbage(state, 0);
#endif

    // if NULL is passed, realloc() acts like malloc()
    void *newBuf = realloc(buf, newSize);

    if (newBuf == NULL) {
        CERROR("failed to allocate memory!");
        exit(1);
    }

    return newBuf;
}

COSMO_API bool cosmoM_checkGarbage(CState *state, size_t needed) {
    if (!(cosmoM_isFrozen(state)) && state->allocatedBytes + needed > state->nextGC) {
        cosmoM_collectGarbage(state); // cya lol
        return true;
    }

    return false;
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
        if (IS_OBJ(entry->key) && !(cosmoV_readObj(entry->key))->isMarked) { // if the key is a object and it's white (unmarked), remove it from the table
            cosmoT_remove(state, tbl, entry->key);
        }
    }

    cosmoT_checkShrink(state, tbl); // recovers the memory we're no longer using
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
        case COBJ_DICT: { // dictionaries are just wrappers for CTable
            CObjDict *dict = (CObjDict*)obj;
            markTable(state, &dict->tbl);
            break;
        }
        case COBJ_UPVALUE: {
            markValue(state, ((CObjUpval*)obj)->closed);
            break;
        }
        case COBJ_FUNCTION: {
            CObjFunction *func = (CObjFunction*)obj;
            markObject(state, (CObj*)func->name);
            markObject(state, (CObj*)func->module);
            markArray(state, &func->chunk.constants);

            break;
        }
        case COBJ_METHOD: {
            CObjMethod *method = (CObjMethod*)obj;
            markValue(state, method->func);
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

    // we can use cosmoM_growaraay because we lock the GC when we entered in cosmoM_collectGarbage
    cosmoM_growarray(state, CObj*, state->grayStack.array, state->grayStack.count, state->grayStack.capacity);

    state->grayStack.array[state->grayStack.count++] = obj;
}

void markValue(CState *state, CValue val) {
    if (IS_OBJ(val))
        markObject(state, cosmoV_readObj(val));
}

// trace our gray references
void traceGrays(CState *state) {
    while (state->grayStack.count > 0) {
        CObj* obj = state->grayStack.array[--state->grayStack.count];
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

void markUserRoots(CState *state) {
    CObj *root = state->userRoots;

    // traverse userRoots and mark all the object
    while (root != NULL) {
        markObject(state, root);
        root = root->nextRoot;
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

    // mark the user defined roots
    markUserRoots(state);

    // mark our proto object
    markObject(state, (CObj*)state->protoObj);
    traceGrays(state);
}

COSMO_API void cosmoM_collectGarbage(CState *state) {
#ifdef GC_DEBUG
    printf("-- GC start\n");
    size_t start = state->allocatedBytes;
#endif
    cosmoM_freezeGC(state); // we don't want a recursive garbage collection event!

    markRoots(state);

    tableRemoveWhite(state, &state->strings); // make sure we aren't referencing any strings that are about to be free'd
    // now finally, free all the unmarked objects
    sweep(state);

    // set our next GC event
    cosmoM_updateThreshhold(state);

    state->freezeGC--; // we don't want to use cosmoM_unfreezeGC because that might trigger a GC event (if GC_STRESS is defined)
#ifdef GC_DEBUG
    printf("-- GC end, reclaimed %ld bytes (started at %ld, ended at %ld), next garbage collection scheduled at %ld bytes\n",
            start - state->allocatedBytes, start, state->allocatedBytes, state->nextGC);
    getchar(); // pauses execution
#endif
}

COSMO_API void cosmoM_updateThreshhold(CState *state) {
    state->nextGC = state->allocatedBytes * HEAP_GROW_FACTOR;
}

COSMO_API void cosmoM_addRoot(CState *state, CObj *newRoot) {
    // first, check and make sure this root doesn't already exist in the list
    CObj *root = state->userRoots;
    while (root != NULL) {
        if (root == newRoot) // found in the list, abort
            return;
        
        root = root->nextRoot;
    }
    
    // adds root to userRoot linked list
    newRoot->nextRoot = state->userRoots;
    state->userRoots = newRoot;
}

COSMO_API void cosmoM_removeRoot(CState *state, CObj *oldRoot) {
    CObj *prev = NULL;
    CObj *root = state->userRoots;

    // traverse the userRoot linked list
    while (root != NULL) {
        if (root == oldRoot) { // found root in list

            // remove from the linked list
            if (prev == NULL) {
                state->userRoots = root->nextRoot;
            } else {
                prev->nextRoot = root->nextRoot;
            }

            root->nextRoot = NULL;
            break;
        }

        prev = root;
        root = root->nextRoot;
    }
}