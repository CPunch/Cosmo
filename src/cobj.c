#include "cobj.h"

#include "clex.h"
#include "cmem.h"
#include "cstate.h"
#include "ctable.h"
#include "cvm.h"

#include <stdarg.h>
#include <string.h>

// we don't actually hash the whole string :eyes:
uint32_t hashString(const char *str, size_t sz)
{
    uint32_t hash = sz;
    size_t step = (sz >> 5) + 1;

    for (size_t i = sz; i >= step; i -= step)
        hash = ((hash << 5) + (hash >> 2)) + str[i - 1];

    return hash;
}

CObj *cosmoO_allocateBase(CState *state, size_t sz, CObjType type)
{
    CObj *obj = (CObj *)cosmoM_xmalloc(state, sz);
    obj->type = type;
    obj->isMarked = false;
    obj->proto = state->protoObjects[type];

    obj->next = state->objects;
    state->objects = obj;

    obj->nextRoot = NULL;
#ifdef GC_DEBUG
    printf("allocated %s %p\n", cosmoO_typeStr(obj), obj);
#endif
    return obj;
}

void cosmoO_free(CState *state, CObj *obj)
{
#ifdef GC_DEBUG
    printf("freeing %s %p\n", cosmoO_typeStr(obj), obj);
#endif
    switch (obj->type) {
    case COBJ_STRING: {
        CObjString *objStr = (CObjString *)obj;
        cosmoM_freearray(state, char, objStr->str, objStr->length + 1);
        cosmoM_free(state, CObjString, objStr);
        break;
    }
    case COBJ_OBJECT: {
        CObjObject *objTbl = (CObjObject *)obj;
        cosmoT_clearTable(state, &objTbl->tbl);
        cosmoM_free(state, CObjObject, objTbl);
        break;
    }
    case COBJ_TABLE: {
        CObjTable *tbl = (CObjTable *)obj;
        cosmoT_clearTable(state, &tbl->tbl);
        cosmoM_free(state, CObjTable, tbl);
        break;
    }
    case COBJ_UPVALUE: {
        cosmoM_free(state, CObjUpval, obj);
        break;
    }
    case COBJ_FUNCTION: {
        CObjFunction *objFunc = (CObjFunction *)obj;
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
        CObjError *err = (CObjError *)obj;
        cosmoM_freearray(state, CCallFrame, err->frames, err->frameCount);
        cosmoM_free(state, CObjError, obj);
        break;
    }
    case COBJ_CLOSURE: {
        CObjClosure *closure = (CObjClosure *)obj;
        cosmoM_freearray(state, CObjUpval *, closure->upvalues, closure->upvalueCount);
        cosmoM_free(state, CObjClosure, closure);
        break;
    }
    case COBJ_MAX:
    default: { /* stubbed, should never happen */
    }
    }
}

bool cosmoO_equal(CState *state, CObj *obj1, CObj *obj2)
{
    CObjObject *proto1, *proto2;
    CValue eq1, eq2;

    if (obj1 == obj2) // its the same reference, this compares strings for us since they're interned
                      // anyways :)
        return true;

    // its not the same type, maybe both <ref>'s have the same '__equal' metamethod in their protos?
    if (obj1->type != obj2->type)
        goto _eqFail;

    switch (obj1->type) {
    case COBJ_STRING: {
        /*
            we already compared the pointers at the top of the function, this prevents the `__equal`
           metamethod from being checked. If you plan on using `__equal` with strings just remove
           this case!
        */
        return false;
    }
    case COBJ_CFUNCTION: {
        CObjCFunction *cfunc1 = (CObjCFunction *)obj1;
        CObjCFunction *cfunc2 = (CObjCFunction *)obj2;
        if (cfunc1->cfunc == cfunc2->cfunc)
            return true;
        goto _eqFail;
    }
    case COBJ_METHOD: {
        CObjMethod *method1 = (CObjMethod *)obj1;
        CObjMethod *method2 = (CObjMethod *)obj2;
        if (cosmoV_equal(state, method1->func, method2->func))
            return true;
        goto _eqFail;
    }
    case COBJ_CLOSURE: {
        CObjClosure *closure1 = (CObjClosure *)obj1;
        CObjClosure *closure2 = (CObjClosure *)obj2;
        // we just compare the function pointer
        if (closure1->function == closure2->function)
            return true;
        goto _eqFail;
    }
    default:
        goto _eqFail;
    }

_eqFail:
    // this is pretty expensive (bad lookup caching helps a lot), but it only all gets run if both
    // objects have protos & both have the `__equal` metamethod defined so... it should stay light
    // for the majority of cases
    if ((proto1 = cosmoO_grabProto(obj1)) != NULL &&
        (proto2 = cosmoO_grabProto(obj2)) != NULL && // make sure both protos exist
        cosmoO_getIString(
            state, proto1, ISTRING_EQUAL,
            &eq1) && // grab the `__equal` metamethod from the first proto, if fail abort
        cosmoO_getIString(
            state, proto2, ISTRING_EQUAL,
            &eq2) && // grab the `__equal` metamethod from the second proto, if fail abort
        cosmoV_equal(state, eq1, eq2)) { // compare the two `__equal` metamethods

        // now finally, call the `__equal` metamethod (<object>, <object>)
        cosmoV_pushValue(state, eq1);
        cosmoV_pushRef(state, obj1);
        cosmoV_pushRef(state, obj2);
        cosmoV_call(state, 2, 1);

        // check return value and make sure it's a boolean
        if (!IS_BOOLEAN(*cosmoV_getTop(state, 0))) {
            cosmoV_error(state, "__equal expected to return <boolean>, got %s!",
                         cosmoV_typeStr(*cosmoV_pop(state)));
        }

        // return the result
        return cosmoV_readBoolean(*cosmoV_pop(state));
    }

    return false;
}

CObjObject *cosmoO_newObject(CState *state)
{
    CObjObject *obj = (CObjObject *)cosmoO_allocateBase(state, sizeof(CObjObject), COBJ_OBJECT);
    obj->istringFlags = 0;
    obj->userP = NULL; // reserved for C API
    obj->userT = 0;
    obj->isLocked = false;

    cosmoV_pushRef(state, (CObj *)obj); // so our GC can keep track of it
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);
    return obj;
}

CObjTable *cosmoO_newTable(CState *state)
{
    CObjTable *obj = (CObjTable *)cosmoO_allocateBase(state, sizeof(CObjTable), COBJ_TABLE);

    // init the table (might cause a GC event)
    cosmoV_pushRef(state, (CObj *)obj); // so our GC can keep track of obj
    cosmoT_initTable(state, &obj->tbl, ARRAY_START);
    cosmoV_pop(state);

    return obj;
}

CObjFunction *cosmoO_newFunction(CState *state)
{
    CObjFunction *func =
        (CObjFunction *)cosmoO_allocateBase(state, sizeof(CObjFunction), COBJ_FUNCTION);
    func->args = 0;
    func->upvals = 0;
    func->variadic = false;
    func->name = NULL;
    func->module = NULL;

    initChunk(state, &func->chunk, ARRAY_START);
    return func;
}

CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func)
{
    CObjCFunction *cfunc =
        (CObjCFunction *)cosmoO_allocateBase(state, sizeof(CObjCFunction), COBJ_CFUNCTION);
    cfunc->cfunc = func;
    return cfunc;
}

CObjError *cosmoO_newError(CState *state, CValue err)
{
    CCallFrame *frames = cosmoM_xmalloc(state, sizeof(CCallFrame) * state->frameCount);
    CObjError *cerror = (CObjError *)cosmoO_allocateBase(state, sizeof(CObjError), COBJ_ERROR);
    cerror->err = err;
    cerror->frameCount = state->frameCount;
    cerror->frames = frames;
    cerror->parserError = false;

    // clone the call frame
    for (int i = 0; i < state->frameCount; i++)
        cerror->frames[i] = state->callFrame[i];

    return cerror;
}

CObjMethod *cosmoO_newMethod(CState *state, CValue func, CObj *obj)
{
    CObjMethod *method = (CObjMethod *)cosmoO_allocateBase(state, sizeof(CObjMethod), COBJ_METHOD);
    method->func = func;
    method->obj = obj;
    return method;
}

CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func)
{
    // initialize array of pointers
    CObjUpval **upvalues = cosmoM_xmalloc(state, sizeof(CObjUpval *) * func->upvals);

    for (int i = 0; i < func->upvals; i++) {
        upvalues[i] = NULL;
    }

    CObjClosure *closure =
        (CObjClosure *)cosmoO_allocateBase(state, sizeof(CObjClosure), COBJ_CLOSURE);
    closure->function = func;
    closure->upvalues = upvalues;
    closure->upvalueCount = func->upvals;

    return closure;
}

CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val)
{
    CObjUpval *upval = (CObjUpval *)cosmoO_allocateBase(state, sizeof(CObjUpval), COBJ_UPVALUE);
    upval->val = val;
    upval->closed = cosmoV_newNil();
    upval->next = NULL;

    return upval;
}

CObjString *cosmoO_copyString(CState *state, const char *str, size_t length)
{
    uint32_t hash = hashString(str, length);
    CObjString *lookup = cosmoT_lookupString(&state->strings, str, length, hash);

    // have we already interned this string?
    if (lookup != NULL)
        return lookup;

    char *buf = cosmoM_xmalloc(state, sizeof(char) * (length + 1)); // +1 for null terminator
    memcpy(buf, str, length);                                       // copy string to heap
    buf[length] = '\0'; // don't forget our null terminator

    return cosmoO_allocateString(state, buf, length, hash);
}

// length shouldn't include the null terminator! str should be a null terminated string! (char array
// should also have been allocated using cosmoM_xmalloc!)
CObjString *cosmoO_takeString(CState *state, char *str, size_t length)
{
    uint32_t hash = hashString(str, length);

    CObjString *lookup = cosmoT_lookupString(&state->strings, str, length, hash);

    // have we already interned this string?
    if (lookup != NULL) {
        cosmoM_freearray(state, char, str,
                         length + 1); // free our passed character array, it's unneeded!
        return lookup;
    }

    return cosmoO_allocateString(state, str, length, hash);
}

CObjString *cosmoO_allocateString(CState *state, const char *str, size_t sz, uint32_t hash)
{
    CObjString *strObj = (CObjString *)cosmoO_allocateBase(state, sizeof(CObjString), COBJ_STRING);
    strObj->isIString = false;
    strObj->str = (char *)str;
    strObj->length = sz;
    strObj->hash = hash;

    // we push & pop the string so our GC can find it (we don't use freezeGC/unfreezeGC because we
    // *want* a GC event to happen)
    cosmoV_pushRef(state, (CObj *)strObj);
    cosmoT_insert(state, &state->strings, cosmoV_newRef((CObj *)strObj));
    cosmoV_pop(state);

    return strObj;
}

CObjString *cosmoO_pushVFString(CState *state, const char *format, va_list args)
{
    StkPtr start = state->top;
    const char *end;
    char c;
    int len;

    while (true) {
        end = strchr(format, '%'); // grab the next occurrence of '%'
        len = -1;                  // -1 means no length specified
        if (end == NULL)           // the end, no '%' found
            break;

        // push the string before '%'
        cosmoV_pushLString(state, format, (end - format));

    reentry:
        c = end[1]; // the character right after '%'
        switch (c) {
        case 'd': // int
            cosmoV_pushNumber(state, va_arg(args, int));
            break;
        case 'f': // double
            cosmoV_pushNumber(state, va_arg(args, double));
            break;
        case 's':         // char *
            if (len >= 0) // the length is specified
                cosmoV_pushLString(state, va_arg(args, char *), len);
            else
                cosmoV_pushString(state, va_arg(args, char *));
            break;
        case '*': // length specifier
            len = va_arg(args, int);
            end++; // skip '*'
            goto reentry;
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
    cosmoV_concat(state,
                  state->top - start); // use cosmoV_concat to concat all the strings on the stack
    return cosmoV_readString(*start);  // start should be state->top - 1
}

// walks the protos of obj and checks for proto
bool cosmoO_isDescendant(CObj *obj, CObjObject *proto)
{
    CObjObject *curr = obj->proto;

    while (curr != NULL) {
        if (curr == proto)
            return true; // found proto! return true

        curr = ((CObj *)curr)->proto;
    }

    // we didn't find the proto
    return false;
}

// returns false if error thrown
bool cosmoO_getRawObject(CState *state, CObjObject *proto, CValue key, CValue *val, CObj *obj)
{
    if (!cosmoT_get(state, &proto->tbl, key,
                    val)) { // if the field doesn't exist in the object, check the proto
        if (cosmoO_getIString(state, proto, ISTRING_GETTER, val) && IS_TABLE(*val) &&
            cosmoT_get(state, &cosmoV_readTable(*val)->tbl, key, val)) {
            cosmoV_pushValue(state, *val);      // push function
            cosmoV_pushRef(state, (CObj *)obj); // push object
            cosmoV_call(state, 1, 1);           // call the function with the 1 argument
            *val = *cosmoV_pop(state);          // set value to the return value of __index
            return true;
        }

        if (proto->_obj.proto != NULL &&
            cosmoO_getRawObject(state, proto->_obj.proto, key, val, obj))
            return true;

        *val = cosmoV_newNil();
        return true; // no protoobject to check against / key not found
    }

    return true;
}

void cosmoO_setRawObject(CState *state, CObjObject *proto, CValue key, CValue val, CObj *obj)
{
    CValue ret;

    // if the object is locked, throw an error
    if (proto->isLocked) {
        cosmoV_error(state, "Couldn't set on a locked object!");
        return;
    }

    // check for __setters
    if (cosmoO_getIString(state, proto, ISTRING_SETTER, &ret) && IS_TABLE(ret) &&
        cosmoT_get(state, &cosmoV_readTable(ret)->tbl, key, &ret)) {
        cosmoV_pushValue(state, ret);       // push function
        cosmoV_pushRef(state, (CObj *)obj); // push object
        cosmoV_pushValue(state, val);       // push new value
        cosmoV_call(state, 2, 0);
        return;
    }

    // if the key is an IString, we need to reset the cache
    if (IS_STRING(key) && cosmoV_readString(key)->isIString)
        proto->istringFlags = 0; // reset cache

    if (IS_NIL(val)) { // if we're setting an index to nil, we can safely mark that as a tombstone
        cosmoT_remove(state, &proto->tbl, key);
    } else {
        CValue *newVal = cosmoT_insert(state, &proto->tbl, key);
        *newVal = val;
    }
}

void cosmoO_setUserP(CObjObject *object, void *p)
{
    object->userP = p;
}

void *cosmoO_getUserP(CObjObject *object)
{
    return object->userP;
}

void cosmoO_setUserI(CObjObject *object, int i)
{
    object->userI = i;
}

int cosmoO_getUserI(CObjObject *object)
{
    return object->userI;
}

void cosmoO_setUserT(CObjObject *object, int t)
{
    object->userT = t;
}

int cosmoO_getUserT(CObjObject *object)
{
    return object->userT;
}

void cosmoO_lock(CObjObject *object)
{
    object->isLocked = true;
}

void cosmoO_unlock(CObjObject *object)
{
    object->isLocked = false;
}

bool rawgetIString(CState *state, CObjObject *object, int flag, CValue *val)
{
    if (readFlag(object->istringFlags, flag))
        return false; // it's been cached as bad

    if (!cosmoT_get(state, &object->tbl, cosmoV_newRef(state->iStrings[flag]), val)) {
        // mark it bad!
        setFlagOn(object->istringFlags, flag);
        return false;
    }

    return true; // :)
}

bool cosmoO_getIString(CState *state, CObjObject *object, int flag, CValue *val)
{
    CObjObject *obj = object;

    do {
        if (rawgetIString(state, obj, flag, val))
            return true;
    } while ((obj = obj->_obj.proto) != NULL); // sets obj to it's proto and compares it to NULL

    return false; // obj->proto was false, the istring doesn't exist in this object chain
}

bool cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val)
{
    if (cosmoO_getIString(state, object, ISTRING_INDEX, val)) {
        cosmoV_pushValue(state, *val);         // push function
        cosmoV_pushRef(state, (CObj *)object); // push object
        cosmoV_pushValue(state, key);          // push key
        cosmoV_call(state, 2, 1);              // call the function with the 2 arguments
        *val = *cosmoV_pop(state);             // set value to the return value of __index
        return true;
    } else { // there's no __index function defined!
        cosmoV_error(state, "Couldn't index object without __index function!");
    }

    return false;
}

bool cosmoO_newIndexObject(CState *state, CObjObject *object, CValue key, CValue val)
{
    CValue ret; // return value for cosmoO_getIString

    if (cosmoO_getIString(state, object, ISTRING_NEWINDEX, &ret)) {
        cosmoV_pushValue(state, ret);          // push function
        cosmoV_pushRef(state, (CObj *)object); // push object
        cosmoV_pushValue(state, key);          // push key & value pair
        cosmoV_pushValue(state, val);
        cosmoV_call(state, 3, 0);
        return true;
    } else { // there's no __newindex function defined
        cosmoV_error(state, "Couldn't set index on object without __newindex function!");
    }

    return false;
}

CObjString *cosmoO_toString(CState *state, CObj *obj)
{
    CObjObject *protoObject = cosmoO_grabProto(obj);
    CValue res;

    // use user-defined __tostring
    if (protoObject != NULL && cosmoO_getIString(state, protoObject, ISTRING_TOSTRING, &res)) {
        cosmoV_pushValue(state, res);
        cosmoV_pushRef(state, (CObj *)obj);
        cosmoV_call(state, 1, 1);

        // make sure the __tostring function returned a string
        StkPtr ret = cosmoV_getTop(state, 0);
        if (!IS_STRING(*ret)) {
            cosmoV_error(state, "__tostring expected to return <string>, got %s!",
                         cosmoV_typeStr(*ret));
            return cosmoO_copyString(state, "<err>", 5);
        }

        // return string
        cosmoV_pop(state);
        return (CObjString *)cosmoV_readRef(*ret);
    }

    switch (obj->type) {
    case COBJ_STRING: {
        return (CObjString *)obj;
    }
    case COBJ_CLOSURE: { // should be transparent to the user imo
        CObjClosure *closure = (CObjClosure *)obj;
        return cosmoO_toString(state, (CObj *)closure->function);
    }
    case COBJ_FUNCTION: {
        CObjFunction *func = (CObjFunction *)obj;
        return func->name != NULL ? func->name
                                  : cosmoO_copyString(state, UNNAMEDCHUNK, strlen(UNNAMEDCHUNK));
    }
    case COBJ_CFUNCTION: {
        CObjCFunction *cfunc = (CObjCFunction *)obj;
        char buf[64];
        int sz =
            sprintf(buf, "<c function> %p", (void *)cfunc->cfunc) + 1; // +1 for the null character
        return cosmoO_copyString(state, buf, sz);
    }
    case COBJ_OBJECT: {
        char buf[64];
        int sz = sprintf(buf, "<obj> %p", (void *)obj) + 1; // +1 for the null character
        return cosmoO_copyString(state, buf, sz);
    }
    case COBJ_ERROR: {
        CObjError *err = (CObjError *)obj;
        return cosmoV_toString(state, err->err);
    }
    case COBJ_TABLE: {
        char buf[64];
        int sz = sprintf(buf, "<tbl> %p", (void *)obj) + 1; // +1 for the null character
        return cosmoO_copyString(state, buf, sz);
    }
    default: {
        char buf[64];
        int sz = sprintf(buf, "<unkn obj> %p", (void *)obj) + 1; // +1 for the null character
        return cosmoO_copyString(state, buf, sz);
    }
    }
}

cosmo_Number cosmoO_toNumber(CState *state, CObj *obj)
{
    CObjObject *proto = cosmoO_grabProto(obj);
    CValue res;

    if (proto != NULL && cosmoO_getIString(state, proto, ISTRING_TONUMBER, &res)) {
        cosmoV_pushValue(state, res);
        cosmoV_pushRef(state, (CObj *)obj);
        cosmoV_call(state, 1, 1); // call res, expect 1 return val of <number>

        StkPtr temp = cosmoV_getTop(state, 0);
        if (!IS_NUMBER(*temp)) {
            cosmoV_error(state, "__tonumber expected to return <number>, got %s!",
                         cosmoV_typeStr(*temp));
        }

        // return number
        cosmoV_pop(state);
        return cosmoV_readNumber(*temp);
    }

    switch (obj->type) {
    case COBJ_STRING: {
        CObjString *str = (CObjString *)obj;
        return strtod(str->str, NULL);
    }
    default: // maybe in the future throw an error?
        return 0;
    }
}

int cosmoO_count(CState *state, CObj *obj)
{
    CObjObject *proto = cosmoO_grabProto(obj);
    CValue res;

    if (proto != NULL && cosmoO_getIString(state, proto, ISTRING_COUNT, &res)) {
        cosmoV_pushValue(state, res);
        cosmoV_pushRef(state, (CObj *)obj);
        cosmoV_call(state, 1, 1); // call res, we expect 1 return value of type <number>

        StkPtr ret = cosmoV_getTop(state, 0);
        if (!IS_NUMBER(*ret)) {
            cosmoV_error(state, "__count expected to return <number>, got %s!",
                         cosmoV_typeStr(*ret));
            return 0;
        }

        // return number
        cosmoV_pop(state);
        return (int)cosmoV_readNumber(*ret);
    }

    switch (obj->type) {
    case COBJ_TABLE: { // returns the # of entries in the hash table
        CObjTable *tbl = (CObjTable *)obj;
        return cosmoT_count(&tbl->tbl);
    }
    case COBJ_STRING: { // returns the length of the string
        CObjString *str = (CObjString *)obj;
        return str->length;
    }
    default:
        cosmoV_error(state, "Couldn't get # (count) of %s!", cosmoO_typeStr(obj));
        return 0;
    }
}

void printObject(CObj *o)
{
    printf("%s ", cosmoO_typeStr(o));
    switch (o->type) {
    case COBJ_STRING: {
        CObjString *objStr = (CObjString *)o;
        printf("\"%.*s\"", objStr->length, objStr->str);
        break;
    }
    case COBJ_OBJECT: {
        printf("%p", (void *)o);
        break;
    }
    case COBJ_TABLE: {
        CObjTable *tbl = (CObjTable *)o;
        printf("%p", (void *)tbl);
        break;
    }
    case COBJ_FUNCTION: {
        CObjFunction *objFunc = (CObjFunction *)o;
        if (objFunc->name != NULL)
            printf("%.*s", objFunc->name->length, objFunc->name->str);
        else
            printf("%s", UNNAMEDCHUNK);
        break;
    }
    case COBJ_CFUNCTION: {
        CObjCFunction *objCFunc = (CObjCFunction *)o;
        printf("%p", (void *)objCFunc->cfunc);
        break;
    }
    case COBJ_ERROR: {
        CObjError *err = (CObjError *)o;
        printf("%p -> ", (void *)o);
        printValue(err->err);
        break;
    }
    case COBJ_METHOD: {
        CObjMethod *method = (CObjMethod *)o;
        printf("%p -> ", (void *)method);
        printValue(method->func);
        break;
    }
    case COBJ_CLOSURE: {
        CObjClosure *closure = (CObjClosure *)o;
        printf("%p -> ", (void *)closure);
        printObject((CObj *)closure->function); // just print the function
        break;
    }
    case COBJ_UPVALUE: {
        CObjUpval *upval = (CObjUpval *)o;
        printf("%p -> ", (void *)upval->val);
        printValue(*upval->val);
        break;
    }
    default:
        printf("%p", (void *)o);
    }
}

const char *cosmoO_typeStr(CObj *obj)
{
    switch (obj->type) {
    case COBJ_STRING:
        return "<string>";
    case COBJ_OBJECT:
        return "<object>";
    case COBJ_TABLE:
        return "<table>";
    case COBJ_FUNCTION:
        return "<function>";
    case COBJ_CFUNCTION:
        return "<c function>";
    case COBJ_ERROR:
        return "<error>";
    case COBJ_METHOD:
        return "<method>";
    case COBJ_CLOSURE:
        return "<closure>";
    case COBJ_UPVALUE:
        return "<upvalue>";

    default:
        return "<unkn obj>"; // TODO: maybe panic? could be a malformed object :eyes:
    }
}
