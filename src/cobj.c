#include "cstate.h"
#include "ctable.h"
#include "cobj.h"
#include "cmem.h"

#include <string.h>

// we don't actually hash the whole string :eyes:
uint32_t hashString(const char *str, size_t sz) {
    uint32_t hash = sz;
    size_t step = (sz>>5)+1;

    for (int i = sz; i >= step; i-=step)
        hash = ((hash << 5) + (hash>>2)) + str[i-1];

    return hash;
}

CObj *cosmoO_allocateBase(CState *state, size_t sz, CObjType type) {
    CObj* obj = (CObj*)cosmoM_xmalloc(state, sz);
    obj->type = type;
    obj->isMarked = false;

    obj->next = state->objects;
    state->objects = obj;
#ifdef GC_DEBUG
    printf("allocated %p with OBJ_TYPE %d\n", obj, type);
#endif
    return obj;
}

void cosmoO_free(CState *state, CObj* obj) {
#ifdef GC_DEBUG
    printf("freeing %p [", obj);
    printObject(obj);
    printf("]\n");
#endif
    switch(obj->type) {
        case COBJ_STRING: {
            CObjString *objStr = (CObjString*)obj;
            cosmoM_freearray(state, char, objStr->str, objStr->length);
            cosmoM_free(state, CObjString, objStr);
            break;
        }
        case COBJ_OBJECT: {
            CObjObject *objTbl = (CObjObject*)obj;
            cosmoT_clearTable(state, &objTbl->tbl);
            cosmoM_free(state, CObjObject, objTbl);
            break;
        }
        case COBJ_UPVALUE: {
            cosmoM_free(state, CObjUpval, obj);
            break;
        }
        case COBJ_FUNCTION: {
            CObjFunction *objFunc = (CObjFunction*)obj;
            cleanChunk(state, &objFunc->chunk);
            cosmoM_free(state, CObjFunction, objFunc);
            break;
        }
        case COBJ_CFUNCTION: {
            cosmoM_free(state, CObjCFunction, obj);
            break;
        }
        case COBJ_METHOD: {
            cosmoM_free(state, CObjMethod, obj); // we don't own the closure or the object so /shrug
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure* closure = (CObjClosure*)obj;
            cosmoM_freearray(state, CObjUpval*, closure->upvalues, closure->upvalueCount);
            cosmoM_free(state, CObjClosure, closure);
            break;
        }
    }
}

bool cosmoO_equal(CObj* obj1, CObj* obj2) {
    if (obj1->type != obj2->type)
        return false;

    switch (obj1->type) {
        case COBJ_STRING:
            return obj1 == obj2; // compare pointers because we already intern all strings :)
        case COBJ_CFUNCTION: {
            CObjCFunction *cfunc1 = (CObjCFunction*)obj1;
            CObjCFunction *cfunc2 = (CObjCFunction*)obj2;
            return cfunc1->cfunc == cfunc2->cfunc;
        }
        default:
            return false;
    }
}

CObjObject *cosmoO_newObject(CState *state) {
    CObjObject *obj = (CObjObject*)cosmoO_allocateBase(state, sizeof(CObjObject), COBJ_OBJECT);
    obj->meta = state->metaObj;
    cosmoV_pushValue(state, cosmoV_newObj(obj)); // so out GC can keep track of it
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);

    return obj;
}

CObjFunction *cosmoO_newFunction(CState *state) {
    CObjFunction *func = (CObjFunction*)cosmoO_allocateBase(state, sizeof(CObjFunction), COBJ_FUNCTION);
    func->args = 0;
    func->upvals = 0;
    func->name = NULL;

    initChunk(state, &func->chunk, ARRAY_START);
    return func;
}

CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func) {
    CObjCFunction *cfunc = (CObjCFunction*)cosmoO_allocateBase(state, sizeof(CObjCFunction), COBJ_CFUNCTION);
    cfunc->cfunc = func;
    return cfunc;
}

CObjMethod *cosmoO_newCMethod(CState *state, CObjCFunction *func, CObjObject *obj) {
    CObjMethod *method = (CObjMethod*)cosmoO_allocateBase(state, sizeof(CObjMethod), COBJ_METHOD);
    method->cfunc = func;
    method->obj = obj;
    method->isCFunc = true;
    return method;
}

CObjMethod *cosmoO_newMethod(CState *state, CObjClosure *func, CObjObject *obj) {
    CObjMethod *method = (CObjMethod*)cosmoO_allocateBase(state, sizeof(CObjMethod), COBJ_METHOD);
    method->closure = func;
    method->obj = obj;
    method->isCFunc = false;
    return method;
}

CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func) {
    // intialize array of pointers
    CObjUpval **upvalues = cosmoM_xmalloc(state, sizeof(CObjUpval*) * func->upvals);

    for (int i = 0; i < func->upvals; i++) {
        upvalues[i] = NULL;
    }

    CObjClosure *closure = (CObjClosure*)cosmoO_allocateBase(state, sizeof(CObjClosure), COBJ_CLOSURE);
    closure->function = func;
    closure->upvalues = upvalues;
    closure->upvalueCount = func->upvals;

    return closure;
}

CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val) {
    CObjUpval *upval = (CObjUpval*)cosmoO_allocateBase(state, sizeof(CObjUpval), COBJ_UPVALUE);
    upval->val = val;
    upval->closed = cosmoV_newNil();
    upval->next = NULL;

    return upval;
}

CObjString *cosmoO_copyString(CState *state, const char *str, size_t sz) {
    uint32_t hash = hashString(str, sz);
    CObjString *lookup = cosmoT_lookupString(&state->strings, str, sz, hash);

    // have we already interned this string?
    if (lookup != NULL)
        return lookup;

    char *buf = cosmoM_xmalloc(state, sizeof(char) * (sz + 1)); // +1 for null terminator
    memcpy(buf, str, sz); // copy string to heap
    buf[sz] = '\0'; // don't forget our null terminator

    return cosmoO_allocateString(state, buf, sz, hash);
}

CObjString *cosmoO_takeString(CState *state, char *str, size_t sz) {
    uint32_t hash = hashString(str, sz);

    CObjString *lookup = cosmoT_lookupString(&state->strings, str, sz, hash);

    // have we already interned this string?
    if (lookup != NULL) {
        cosmoM_freearray(state, char, str, sz); // free our passed character array, it's unneeded!
        return lookup;
    }

    return cosmoO_allocateString(state, str, sz, hash);
}

CObjString *cosmoO_allocateString(CState *state, const char *str, size_t sz, uint32_t hash) {
    CObjString *strObj = (CObjString*)cosmoO_allocateBase(state, sizeof(CObjString), COBJ_STRING);
    strObj->str = (char*)str;
    strObj->length = sz;
    strObj->hash = hash;

    // we push & pop the string so our GC can find it (we don't use freezeGC/unfreezeGC because we *want* a GC event to happen)
    cosmoV_pushValue(state, cosmoV_newObj(strObj)); 
    cosmoT_insert(state, &state->strings, cosmoV_newObj((CObj*)strObj));
    cosmoV_pop(state);

    return strObj;
}

bool cosmoO_getObject(CState *state, CObjObject *object, CValue key, CValue *val) {
    if (!cosmoT_get(&object->tbl, key, val) && object->meta != NULL) { // if the field doesn't exist in the object, check the meta
        return cosmoO_getObject(state, object->meta, key, val);
    }

    return true;
}

void cosmoO_setObject(CState *state, CObjObject *object, CValue key, CValue val) {
    CValue *newVal = cosmoT_insert(state, &object->tbl, key);
    *newVal = val;
}

CObjString *cosmoO_toString(CState *state, CObj *obj) {
    switch (obj->type) {
        case COBJ_STRING: {
            return (CObjString*)obj;
        }
        case COBJ_FUNCTION: {
            CObjFunction *func = (CObjFunction*)obj;
            return func->name != NULL ? func->name : cosmoO_copyString(state, UNNAMEDCHUNK, strlen(UNNAMEDCHUNK)); 
        }
        case COBJ_OBJECT: { // TODO: maybe not safe??
            char buf[64];
            int sz = sprintf(buf, "<obj> %p", obj) + 1; // +1 for the null character
            return cosmoO_copyString(state, buf, sz);
        }
        default:
            return cosmoO_copyString(state, "<unkn obj>", 10);
    }
}

void printObject(CObj *o) {
    switch (o->type) {
        case COBJ_STRING: {
            CObjString *objStr = (CObjString*)o;
            printf("\"%.*s\"", objStr->length, objStr->str);
            break;
        }
        case COBJ_OBJECT: {
            printf("<obj> %p", o);
            return;
        }
        case COBJ_UPVALUE: {
            CObjUpval *upval = (CObjUpval*)o;
            printf("<upvalue %p> -> ", upval->val);
            printValue(*upval->val);
            break;
        }
        case COBJ_FUNCTION: {
            CObjFunction *objFunc = (CObjFunction*)o;
            if (objFunc->name != NULL)
                printf("<function> %.*s", objFunc->name->length, objFunc->name->str);
            else
                printf("<function> %s", UNNAMEDCHUNK);
            break;
        }
        case COBJ_CFUNCTION: {
            CObjCFunction *objCFunc = (CObjCFunction*)o;
            printf("<c function> %p", objCFunc->cfunc);
            break;
        }
        case COBJ_METHOD: {
            CObjMethod *method = (CObjMethod*)o;
            if (method->closure->function->name != NULL) {
                printf("<method> %p : %.*s", method->obj, method->closure->function->name->length, method->closure->function->name->str);
            } else {
                printf("<method> %p : %s", method->obj, UNNAMEDCHUNK);
            }
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)o;
            printObject((CObj*)closure->function); // just print the function
            break;
        }
        default:
            printf("<unkn obj>");
    }
}