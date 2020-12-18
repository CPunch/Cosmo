#include "cstate.h"
#include "ctable.h"
#include "cobj.h"
#include "cmem.h"
#include "cvm.h"

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

    obj->nextRoot = NULL;
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
            cosmoM_freearray(state, char, objStr->str, objStr->length + 1);
            cosmoM_free(state, CObjString, objStr);
            break;
        }
        case COBJ_OBJECT: {
            CObjObject *objTbl = (CObjObject*)obj;
            cosmoT_clearTable(state, &objTbl->tbl);
            cosmoM_free(state, CObjObject, objTbl);
            break;
        }
        case COBJ_DICT: {
            CObjDict *dict = (CObjDict*)obj;
            cosmoT_clearTable(state, &dict->tbl);
            cosmoM_free(state, CObjDict, dict);
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
    obj->proto = state->protoObj;
    obj->istringFlags = 0;
    obj->userP = NULL; // reserved for C API
    cosmoV_pushValue(state, cosmoV_newObj(obj)); // so our GC can keep track of it
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);

    return obj;
}

CObjDict *cosmoO_newDictionary(CState *state) {
    CObjDict *obj = (CObjDict*)cosmoO_allocateBase(state, sizeof(CObjDict), COBJ_DICT);

    // init the table (might cause a GC event)
    cosmoV_pushValue(state, cosmoV_newObj(obj)); // so our GC can keep track of obj
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);

    return obj;
}

CObjFunction *cosmoO_newFunction(CState *state) {
    CObjFunction *func = (CObjFunction*)cosmoO_allocateBase(state, sizeof(CObjFunction), COBJ_FUNCTION);
    func->args = 0;
    func->upvals = 0;
    func->name = NULL;
    func->module = NULL;

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
    method->func = cosmoV_newObj(func);
    method->obj = obj;
    return method;
}

CObjMethod *cosmoO_newMethod(CState *state, CObjClosure *func, CObjObject *obj) {
    CObjMethod *method = (CObjMethod*)cosmoO_allocateBase(state, sizeof(CObjMethod), COBJ_METHOD);
    method->func = cosmoV_newObj(func);
    method->obj = obj;
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
    strObj->isIString = false;
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
    if (!cosmoT_get(&object->tbl, key, val)) { // if the field doesn't exist in the object, check the proto
        if (cosmoO_getIString(state, object, ISTRING_GETTER, val) && IS_OBJECT(*val) && cosmoO_getObject(state, cosmoV_readObject(*val), key, val)) {
            cosmoV_pushValue(state, *val); // push function
            cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
            cosmoV_call(state, 1, 1); // call the function with the 1 argument
            *val = *cosmoV_pop(state); // set value to the return value of __index
            return true;
        }
        
        if (object->proto != NULL && cosmoO_getObject(state, object->proto, key, val))
            return true;
        return false; // no protoobject to check against / key not founc
    }

    return true;
}

void cosmoO_setObject(CState *state, CObjObject *object, CValue key, CValue val) {
    CValue ret;

    // first check for __setters
    if (cosmoO_getIString(state, object, ISTRING_SETTER, &ret) && IS_OBJECT(ret) && cosmoO_getObject(state, cosmoV_readObject(ret), key, &ret)) {
        cosmoV_pushValue(state, ret); // push function
        cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
        cosmoV_pushValue(state, val); // push new value
        cosmoV_call(state, 2, 0);
        return;
    }

    // if the key is an IString, we need to reset the cache
    if (IS_STRING(key) && ((CObjString*)cosmoV_readObj(key))->isIString)
        object->istringFlags = 0; // reset cache

    if (IS_NIL(val)) { // if we're setting an index to nil, we can safely mark that as a tombstone
        cosmoT_remove(state, &object->tbl, key);
    } else {
        CValue *newVal = cosmoT_insert(state, &object->tbl, key);
        *newVal = val;
    }
}

void cosmoO_setUserP(CState *state, CObjObject *object, void *p) {
    object->userP = p;
}

void *cosmoO_getUserP(CState *state, CObjObject *object) {
    return object->userP;
}

void cosmoO_setUserI(CState *state, CObjObject *object, int i) {
    object->userI = i;
}

int cosmoO_getUserI(CState *state, CObjObject *object) {
    return object->userI;
}

bool rawgetIString(CState *state, CObjObject *object, int flag, CValue *val) {
    if (readFlag(object->istringFlags, flag))
        return false; // it's been cached as bad

    if (!cosmoT_get(&object->tbl, cosmoV_newObj(state->iStrings[flag]), val)) {
        // mark it bad!
        setFlagOn(object->istringFlags, flag);
        return false;
    }

    return true; // :)
}

bool cosmoO_getIString(CState *state, CObjObject *object, int flag, CValue *val) {
    CObjObject *obj = object;

    do {
        if (rawgetIString(state, obj, flag, val))
            return true;
    } while ((obj = obj->proto) != NULL); // sets obj to it's proto and compares it to NULL

    return false; // obj->proto was false, the istring doesn't exist in this object chain
}

bool cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val) {
    if (cosmoO_getIString(state, object, ISTRING_INDEX, val)) {
        cosmoV_pushValue(state, *val); // push function
        cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
        cosmoV_pushValue(state, key); // push key
        cosmoV_call(state, 2, 1); // call the function with the 2 arguments
        *val = *cosmoV_pop(state); // set value to the return value of __index
        return true;
    } else { // there's no __index function defined!
        cosmoV_error(state, "Couldn't index object without __index function!");
    }

    return false;
}

bool cosmoO_newIndexObject(CState *state, CObjObject *object, CValue key, CValue val) {
    CValue ret; // return value for cosmoO_getIString

    if (cosmoO_getIString(state, object, ISTRING_NEWINDEX, &ret)) {
        cosmoV_pushValue(state, ret); // push function
        cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
        cosmoV_pushValue(state, key); // push key & value pair
        cosmoV_pushValue(state, val);
        cosmoV_call(state, 3, 0);
        return true;
    } else { // there's no __newindex function defined
        cosmoV_error(state, "Couldn't set index on object without __newindex function!");
    }

    return false;
}

CObjString *cosmoO_toString(CState *state, CObj *obj) {
    switch (obj->type) {
        case COBJ_STRING: {
            return (CObjString*)obj;
        }
        case COBJ_CLOSURE: { // should be transparent to the user imo
            CObjClosure *closure = (CObjClosure*)obj;
            return cosmoO_toString(state, (CObj*)closure->function);
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
        case COBJ_DICT: {
            char buf[64];
            int sz = sprintf(buf, "<dict> %p", obj) + 1; // +1 for the null character
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
            break;
        }
        case COBJ_DICT: {
            CObjDict *dict = (CObjDict*)o;
            printf("<dict> %p", dict);
            break;
        }
        case COBJ_UPVALUE: {
            CObjUpval *upval = (CObjUpval*)o;
            printf("<upvalue %p> -> ", upval->val);
            printValue(*upval->val);
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)o;
            printObject((CObj*)closure->function); // just print the function
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
            printf("<method> %p : ", method->obj);
            printValue(method->func);
            break;
        }
        default:
            printf("<unkn obj %p>", o);
    }
}

const char *cosmoO_typeStr(CObj* obj) {
    switch (obj->type) {
        case COBJ_STRING:       return "<string>";
        case COBJ_OBJECT:       return "<object>";
        case COBJ_DICT:         return "<dictionary>";
        case COBJ_FUNCTION:     return "<function>";
        case COBJ_CFUNCTION:    return "<c function>";
        case COBJ_METHOD:       return "<method>";
        case COBJ_CLOSURE:      return "<closure>";
        case COBJ_UPVALUE:      return "<upvalue>";

        default:
            return "<unkn obj>"; // TODO: maybe panic? could be a malformed object :eyes:
    }
}