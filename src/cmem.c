#include "cmem.h"

#include "cbaselib.h"
#include "cobj.h"
#include "cparse.h"
#include "cstate.h"
#include "ctable.h"
#include "cvalue.h"

// realloc wrapper
void *cosmoM_reallocate(CState *state, void *buf, size_t oldSize, size_t newSize, bool isGC)
{
    if (buf == NULL)
        oldSize = 0;

#ifdef GC_DEBUG
    printf("old allocated bytes: %ld\n", state->allocatedBytes);
    if (buf) {
        if (newSize == 0) {
            printf("freeing %p, reclaiming %ld bytes...\n", buf, oldSize);
        } else {
            printf("realloc %p, byte difference: %ld\n", buf, newSize - oldSize);
        }
    }
#endif
    state->allocatedBytes += newSize - oldSize;
#ifdef GC_DEBUG
    printf("new allocated bytes: %ld\n", state->allocatedBytes);
    fflush(stdout);
#endif

    if (newSize == 0) { // it needs to be freed
        free(buf);
        return NULL;
    }

    if (isGC) {
#ifdef GC_STRESS
        if (!(cosmoM_isFrozen(state)) && newSize > oldSize) {
            cosmoM_collectGarbage(state);
        }
#    ifdef GC_DEBUG
        else {
            printf("GC event ignored! state frozen! [%d]\n", state->freezeGC);
        }
#    endif
#else
        cosmoM_checkGarbage(state, 0);
#endif
    }

    // if NULL is passed, realloc() acts like malloc()
    void *newBuf = realloc(buf, newSize);

#ifdef GC_DEBUG
    printf("allocating new buffer of size %ld at %p\n", newSize - oldSize, newBuf);
    fflush(stdout);
#endif

    if (newBuf == NULL) {
        CERROR("failed to allocate memory!");
        exit(1);
    }

    return newBuf;
}

COSMO_API bool cosmoM_checkGarbage(CState *state, size_t needed)
{
    if (!(cosmoM_isFrozen(state)) && state->allocatedBytes + needed > state->nextGC) {
        cosmoM_collectGarbage(state); // cya lol
        return true;
    }

    return false;
}

static void markObject(CState *state, CObj *obj);
static void markValue(CState *state, CValue val);

static void markTable(CState *state, CTable *tbl)
{
    if (tbl->table == NULL) // table is still being initialized
        return;

    int cap = cosmoT_getCapacity(tbl);
    for (int i = 0; i < cap; i++) {
        CTableEntry *entry = &tbl->table[i];
        markValue(state, entry->key);
        markValue(state, entry->val);
    }
}

// removes white members from the table
static void tableRemoveWhite(CState *state, CTable *tbl)
{
    if (tbl->table == NULL) // table is still being initialized
        return;

    int cap = cosmoT_getCapacity(tbl);

#ifdef GC_DEBUG
    printf("tableRemoveWhite: %p, cap: %d\n", tbl, cap);
#endif

    for (int i = 0; i < cap; i++) {
        CTableEntry *entry = &tbl->table[i];
        if (IS_REF(entry->key) &&
            !(cosmoV_readRef(entry->key))->isMarked) { // if the key is a object and it's white
                                                       // (unmarked), remove it from the table
            cosmoT_remove(state, tbl, entry->key);
        }
    }

    cosmoT_checkShrink(state, tbl); // recovers the memory we're no longer using
}

static void markArray(CState *state, CValueArray *array)
{
    for (size_t i = 0; i < array->count; i++) {
        markValue(state, array->values[i]);
    }
}

// mark all references associated with the object
// black = keep, white = discard
static void blackenObject(CState *state, CObj *obj)
{
    markObject(state, (CObj *)obj->proto);
    switch (obj->type) {
    case COBJ_STRING:
    case COBJ_CFUNCTION:
        // stubbed
        break;
    case COBJ_OBJECT: {
        // mark everything this object is keeping track of
        CObjObject *cobj = (CObjObject *)obj;
        markTable(state, &cobj->tbl);
        break;
    }
    case COBJ_TABLE: { // tables are just wrappers for CTable
        CObjTable *tbl = (CObjTable *)obj;
        markTable(state, &tbl->tbl);
        break;
    }
    case COBJ_UPVALUE: {
        markValue(state, ((CObjUpval *)obj)->closed);
        break;
    }
    case COBJ_FUNCTION: {
        CObjFunction *func = (CObjFunction *)obj;
        markObject(state, (CObj *)func->name);
        markObject(state, (CObj *)func->module);
        markArray(state, &func->chunk.constants);

        break;
    }
    case COBJ_METHOD: {
        CObjMethod *method = (CObjMethod *)obj;
        markValue(state, method->func);
        markObject(state, (CObj *)method->obj);
        break;
    }
    case COBJ_ERROR: {
        CObjError *err = (CObjError *)obj;
        markValue(state, err->err);

        // mark callframes
        for (int i = 0; i < err->frameCount; i++)
            markObject(state, (CObj *)err->frames[i].closure);

        break;
    }
    case COBJ_CLOSURE: {
        CObjClosure *closure = (CObjClosure *)obj;
        markObject(state, (CObj *)closure->function);

        // mark all upvalues
        for (int i = 0; i < closure->upvalueCount; i++) {
            markObject(state, (CObj *)closure->upvalues[i]);
        }

        break;
    }
    default:
#ifdef GC_DEBUG
        printf("Unknown type in blackenObject with %p, type %d\n", (void *)obj, obj->type);
#endif
        break;
    }
}

static void markObject(CState *state, CObj *obj)
{
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

    // we can use cosmoM_growarray because we lock the GC when we entered in cosmoM_collectGarbage
    cosmoM_growArrayNonGC(state, CObj *, state->grayStack.array, state->grayStack.count,
                          state->grayStack.capacity);

    state->grayStack.array[state->grayStack.count++] = obj;
}

static void markValue(CState *state, CValue val)
{
    if (IS_REF(val))
        markObject(state, cosmoV_readRef(val));
}

// trace our gray references
static void traceGrays(CState *state)
{
    while (state->grayStack.count > 0) {
        CObj *obj = state->grayStack.array[--state->grayStack.count];
        blackenObject(state, obj);
    }
}

static void sweep(CState *state)
{
    CObj *prev = NULL;
    CObj *object = state->objects;
    while (object != NULL) {
        if (object->isMarked) {       // skip over it
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

static void markUserRoots(CState *state)
{
    CObj *root = state->userRoots;

    // traverse userRoots and mark all the object
    while (root != NULL) {
        markObject(state, root);
        root = root->nextRoot;
    }
}

static void markRoots(CState *state)
{
    // mark all values on the stack
    for (StkPtr value = state->stack; value < state->top; value++) {
        markValue(state, *value);
    }

    // mark all active callframe closures
    for (int i = 0; i < state->frameCount; i++) {
        markObject(state, (CObj *)state->callFrame[i].closure);
    }

    // mark all open upvalues
    for (CObjUpval *upvalue = state->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject(state, (CObj *)upvalue);
    }

    markObject(state, (CObj *)state->globals);

    // mark all internal strings
    for (int i = 0; i < ISTRING_MAX; i++)
        markObject(state, (CObj *)state->iStrings[i]);

    // mark the user defined roots
    markUserRoots(state);

    for (int i = 0; i < COBJ_MAX; i++)
        markObject(state, (CObj *)state->protoObjects[i]);

    traceGrays(state);
}

COSMO_API void cosmoM_collectGarbage(CState *state)
{
#ifdef GC_DEBUG
    printf("-- GC start\n");
    size_t start = state->allocatedBytes;
#endif
    markRoots(state);

    tableRemoveWhite(
        state,
        &state->strings); // make sure we aren't referencing any strings that are about to be freed
    // now finally, free all the unmarked objects
    sweep(state);

    // set our next GC event
    cosmoM_updateThreshhold(state);
#ifdef GC_DEBUG
    printf("-- GC end, reclaimed %ld bytes (started at %ld, ended at %ld), next garbage collection "
           "scheduled at %ld bytes\n",
           start - state->allocatedBytes, start, state->allocatedBytes, state->nextGC);
#endif
}

COSMO_API void cosmoM_updateThreshhold(CState *state)
{
    state->nextGC = state->allocatedBytes * HEAP_GROW_FACTOR;
}

COSMO_API void cosmoM_addRoot(CState *state, CObj *newRoot)
{
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

COSMO_API void cosmoM_removeRoot(CState *state, CObj *oldRoot)
{
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
