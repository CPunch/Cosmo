#include "cstate.h"
#include "ctable.h"
#include "cobj.h"
#include "cmem.h"
#include "cvm.h"
#include "clex.h"

#include <string.h>

// we don't actually hash the whole string :eyes:
uint32_t hashString(const char *str, size_t sz) {
    uint32_t hash = sz;
    size_t step = (sz>>5)+1;

    for (size_t i = sz; i >= step; i-=step)
        hash = ((hash << 5) + (hash>>2)) + str[i-1];

    return hash;
}

CObj *cosmoO_allocateBase(CState *state, size_t sz, CObjType type) {
    CObj* obj = (CObj*)cosmoM_xmalloc(state, sz);
    obj->type = type;
    obj->isMarked = false;
    obj->proto = state->protoObjects[type];

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
        case COBJ_TABLE: {
            CObjTable *tbl = (CObjTable*)obj;
            cosmoT_clearTable(state, &tbl->tbl);
            cosmoM_free(state, CObjTable, tbl);
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
        case COBJ_ERROR: {
            CObjError *err = (CObjError*)obj;
            cosmoM_freearray(state, CCallFrame, err->frames, err->frameCount);
            cosmoM_free(state, CObjError, obj);
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure* closure = (CObjClosure*)obj;
            cosmoM_freearray(state, CObjUpval*, closure->upvalues, closure->upvalueCount);
            cosmoM_free(state, CObjClosure, closure);
            break;
        }
        case COBJ_MAX: { /* stubbed, should never happen */ }
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
    obj->istringFlags = 0;
    obj->userP = NULL; // reserved for C API
    cosmoV_pushValue(state, cosmoV_newObj(obj)); // so our GC can keep track of it
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);

    return obj;
}

CObjTable *cosmoO_newTable(CState *state) {
    CObjTable *obj = (CObjTable*)cosmoO_allocateBase(state, sizeof(CObjTable), COBJ_TABLE);

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
    func->variadic = false;
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

CObjError *cosmoO_newError(CState *state, CValue err) {
    CObjError *cerror = (CObjError*)cosmoO_allocateBase(state, sizeof(CObjError), COBJ_ERROR);
    cerror->err = err;
    cerror->frameCount = state->frameCount;
    cerror->parserError = false;

    // allocate the callframe
    cerror->frames = cosmoM_xmalloc(state, sizeof(CCallFrame) * cerror->frameCount);

    // clone the call frame
    for (int i = 0; i < state->frameCount; i++)
        cerror->frames[i] = state->callFrame[i];
    
    return cerror;
}

CObjMethod *cosmoO_newMethod(CState *state, CValue func, CObj *obj) {
    CObjMethod *method = (CObjMethod*)cosmoO_allocateBase(state, sizeof(CObjMethod), COBJ_METHOD);
    method->func = func;
    method->obj = obj;
    return method;
}

CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func) {
    // initialize array of pointers
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

CObjString *cosmoO_copyString(CState *state, const char *str, size_t length) {
    uint32_t hash = hashString(str, length);
    CObjString *lookup = cosmoT_lookupString(&state->strings, str, length, hash);

    // have we already interned this string?
    if (lookup != NULL)
        return lookup;

    char *buf = cosmoM_xmalloc(state, sizeof(char) * (length + 1)); // +1 for null terminator
    memcpy(buf, str, length); // copy string to heap
    buf[length] = '\0'; // don't forget our null terminator

    return cosmoO_allocateString(state, buf, length, hash);
}

// length shouldn't include the null terminator! (char array should also have been allocated using cosmoM_xmalloc!)
CObjString *cosmoO_takeString(CState *state, char *str, size_t length) {
    uint32_t hash = hashString(str, length);

    CObjString *lookup = cosmoT_lookupString(&state->strings, str, length, hash);

    // have we already interned this string?
    if (lookup != NULL) {
        cosmoM_freearray(state, char, str, length + 1); // free our passed character array, it's unneeded!
        return lookup;
    }

    return cosmoO_allocateString(state, str, length, hash);
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

CObjString *cosmoO_pushVFString(CState *state, const char *format, va_list args) {
    StkPtr start = state->top;

    while (true) {
        const char *end = strchr(format, '%'); // grab the next occurrence of '%'
        if (end == NULL) // the end, no '%' found
            break;

        // push the string before '%'
        cosmoV_pushLString(state, format, (end - format));
        char c = *(end+1); // the character right after '%'

        switch (c) {
            case 'd': { // int
                cosmoV_pushNumber(state, va_arg(args, int));
                break;
            }
            case 'f': { // double
                cosmoV_pushNumber(state, va_arg(args, double));
                break;
            }
            case 's': { // const char *
                cosmoV_pushString(state, va_arg(args, char *));
                break;
            }
            case 't': { // CToken *
                CToken *token = va_arg(args, CToken *);
                cosmoV_pushLString(state, token->start, token->length);
                break;
            }
            default: {
                char temp[2];
                temp[0] = '%';
                temp[1] = c;
                cosmoV_pushLString(state, temp, 2);
            }
        }
        format = end + 2; // + 2 because of % and the following character
    }

    cosmoV_pushString(state, format); // push the rest of the string
    cosmoV_concat(state, state->top - start); // use cosmoV_concat to concat all the strings on the stack
    return cosmoV_readString(*start); // start should be state->top - 1
}

bool cosmoO_getRawObject(CState *state, CObjObject *object, CValue key, CValue *val) {
    if (!cosmoT_get(&object->tbl, key, val)) { // if the field doesn't exist in the object, check the proto
        if (cosmoO_getIString(state, object, ISTRING_GETTER, val) && IS_OBJECT(*val) && cosmoO_getRawObject(state, cosmoV_readObject(*val), key, val)) {
            cosmoV_pushValue(state, *val); // push function
            cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
            if (cosmoV_call(state, 1, 1) != COSMOVM_OK) // call the function with the 1 argument
                return false;
            *val = *cosmoV_pop(state); // set value to the return value of __index
            return true;
        }
        
        if (object->_obj.proto != NULL && cosmoO_getRawObject(state, object->_obj.proto, key, val))
            return true;
        
        *val = cosmoV_newNil();
        return false; // no protoobject to check against / key not found
    }

    return true;
}

void cosmoO_setRawObject(CState *state, CObjObject *object, CValue key, CValue val) {
    CValue ret;

    // first check for __setters
    if (cosmoO_getIString(state, object, ISTRING_SETTER, &ret) && IS_OBJECT(ret) && cosmoO_getRawObject(state, cosmoV_readObject(ret), key, &ret)) {
        cosmoV_pushValue(state, ret); // push function
        cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
        cosmoV_pushValue(state, val); // push new value
        cosmoV_call(state, 2, 0);
        return;
    }

    // if the key is an IString, we need to reset the cache
    if (IS_STRING(key) && cosmoV_readString(key)->isIString)
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
    } while ((obj = obj->_obj.proto) != NULL); // sets obj to it's proto and compares it to NULL

    return false; // obj->proto was false, the istring doesn't exist in this object chain
}

bool cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val) {
    if (cosmoO_getIString(state, object, ISTRING_INDEX, val)) {
        cosmoV_pushValue(state, *val); // push function
        cosmoV_pushValue(state, cosmoV_newObj(object)); // push object
        cosmoV_pushValue(state, key); // push key
        if (cosmoV_call(state, 2, 1) != COSMOVM_OK) // call the function with the 2 arguments
            return false;
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
        return cosmoV_call(state, 3, 0) == COSMOVM_OK;
    } else { // there's no __newindex function defined
        cosmoV_error(state, "Couldn't set index on object without __newindex function!");
    }

    return false;
}

CObjString *cosmoO_toString(CState *state, CObj *obj) {
    CObjObject *protoObject = cosmoO_grabProto(obj);
    CValue res;

    // use user-defined __tostring
    if (protoObject != NULL && cosmoO_getIString(state, protoObject, ISTRING_TOSTRING, &res)) {
        cosmoV_pushValue(state, res);
        cosmoV_pushValue(state, cosmoV_newObj(obj));
        if (cosmoV_call(state, 1, 1) != COSMOVM_OK)
            return cosmoO_copyString(state, "<err>", 5);

        // make sure the __tostring function returned a string
        StkPtr ret = cosmoV_getTop(state, 0);
        if (!IS_STRING(*ret)) {
            cosmoV_error(state, "__tostring expected to return <string>, got %s!", cosmoV_typeStr(*ret));
            return cosmoO_copyString(state, "<err>", 5);
        }

        // return string
        cosmoV_pop(state);
        return (CObjString*)cosmoV_readObj(*ret);
    }

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
        case COBJ_OBJECT: {
            char buf[64];
            int sz = sprintf(buf, "<obj> %p", (void*)obj) + 1; // +1 for the null character
            return cosmoO_copyString(state, buf, sz);
        }
        case COBJ_ERROR: {
            CObjError *err = (CObjError*)obj;
            return cosmoV_toString(state, err->err);
        }
        case COBJ_TABLE: {
            char buf[64];
            int sz = sprintf(buf, "<tbl> %p", (void*)obj) + 1; // +1 for the null character
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
            printf("%.*s", objStr->length, objStr->str);
            break;
        }
        case COBJ_OBJECT: {
            printf("<obj> %p", (void*)o);
            break;
        }
        case COBJ_TABLE: {
            CObjTable *tbl = (CObjTable*)o;
            printf("<tbl> %p", (void*)tbl);
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
            printf("<c function> %p", (void*)objCFunc->cfunc);
            break;
        }
        case COBJ_ERROR: {
            CObjError *err = (CObjError*)o;
            printf("<error> %p -> ", (void*)o);
            printValue(err->err);
            break;
        }
        case COBJ_METHOD: {
            CObjMethod *method = (CObjMethod*)o;
            printf("<method> %p -> ", (void*)method);
            printValue(method->func);
            break;
        }
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)o;
            printf("<closure> %p -> ", (void*)closure);
            printObject((CObj*)closure->function); // just print the function
            break;
        }
        case COBJ_UPVALUE: {
            CObjUpval *upval = (CObjUpval*)o;
            printf("<upvalue> %p -> ", (void*)upval->val);
            printValue(*upval->val);
            break;
        }
        default:
            printf("<unkn obj %p>", (void*)o);
    }
}

const char *cosmoO_typeStr(CObj* obj) {
    switch (obj->type) {
        case COBJ_STRING:       return "<string>";
        case COBJ_OBJECT:       return "<object>";
        case COBJ_TABLE:        return "<table>";
        case COBJ_FUNCTION:     return "<function>";
        case COBJ_CFUNCTION:    return "<c function>";
        case COBJ_METHOD:       return "<method>";
        case COBJ_CLOSURE:      return "<closure>";
        case COBJ_UPVALUE:      return "<upvalue>";

        default:
            return "<unkn obj>"; // TODO: maybe panic? could be a malformed object :eyes:
    }
}
