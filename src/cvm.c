#include "cvm.h"

#include "cdebug.h"
#include "cmem.h"
#include "cparse.h"
#include "cstate.h"
#include "cundump.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

COSMO_API void cosmoV_pushFString(CState *state, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    cosmoO_pushVFString(state, format, args);
    va_end(args);
}

// inserts val at state->top - indx - 1, moving everything else up
COSMO_API void cosmo_insert(CState *state, int indx, CValue val)
{
    StkPtr tmp = cosmoV_getTop(state, indx);

    // moves everything up
    for (StkPtr i = state->top; i > tmp; i--)
        *i = *(i - 1);

    *tmp = val;
    state->top++;
}

COSMO_API bool cosmoV_undump(CState *state, cosmo_Reader reader, const void *ud)
{
    CObjFunction *func;

    if (cosmoD_undump(state, reader, ud, &func)) {
        // fail recovery
        state->panic = false;
        cosmoV_pushRef(state, (CObj *)state->error);
        return false;
    };

    // #ifdef VM_DEBUG
    disasmChunk(&func->chunk, func->name ? func->name->str : UNNAMEDCHUNK, 0);
    // #endif

    // push function onto the stack so it doesn't it cleaned up by the GC, at the same stack
    // location put our closure
    cosmoV_pushRef(state, (CObj *)func);
    *(cosmoV_getTop(state, 0)) = cosmoV_newRef(cosmoO_newClosure(state, func));
    return true;
}

COSMO_API bool cosmoV_compileString(CState *state, const char *src, const char *name)
{
    CObjFunction *func;

    if ((func = cosmoP_compileString(state, src, name)) != NULL) {
        // success
#ifdef VM_DEBUG
        disasmChunk(&func->chunk, func->module->str, 0);
#endif
        // push function onto the stack so it doesn't it cleaned up by the GC, at the same stack
        // location put our closure
        cosmoV_pushRef(state, (CObj *)func);
        *(cosmoV_getTop(state, 0)) = cosmoV_newRef(cosmoO_newClosure(state, func));
        return true;
    }

    // fail recovery
    state->panic = false;
    cosmoV_pushRef(state, (CObj *)state->error);
    return false;
}

COSMO_API void cosmoV_printError(CState *state, CObjError *err)
{
    // print stack trace
    for (int i = 0; i < err->frameCount; i++) {
        CCallFrame *frame = &err->frames[i];
        CObjFunction *function = frame->closure->function;
        CChunk *chunk = &function->chunk;

        int line = chunk->lineInfo[frame->pc - chunk->buf - 1];

        if (i == err->frameCount - 1 &&
            !err->parserError) // it's the last call frame (and not a parser error), prepare for the
                               // objection to be printed
            fprintf(stderr, "Objection in %.*s on [line %d] in ", function->module->length,
                    function->module->str, line);
        else
            fprintf(stderr, "[line %d] in ", line);

        if (function->name == NULL) { // unnamed chunk
            fprintf(stderr, "%s\n", UNNAMEDCHUNK);
        } else {
            fprintf(stderr, "%.*s()\n", function->name->length, function->name->str);
        }
    }

    if (err->parserError)
        fprintf(stderr, "Objection while parsing on [line %d]\n", err->line);

    // finally, print the error message
    CObjString *errString = cosmoV_toString(state, err->err);
    printf("\t%.*s\n", errString->length, errString->str);
}

/*
    takes value on top of the stack and wraps an CObjError around it, state->error is set to that
   value the value on the stack is *expected* to be a string, but not required, so yes, this means
   you could throw a nil value if you really wanted too..
*/
CObjError *cosmoV_throw(CState *state)
{
    StkPtr temp = cosmoV_getTop(state, 0);

    CObjError *error = cosmoO_newError(state, *temp);
    state->error = error;
    state->panic = true;

    cosmoV_pop(state); // pops thrown value off the stack
    return error;
}

void cosmoV_error(CState *state, const char *format, ...)
{
    if (state->panic)
        return;

    // i set panic before calling cosmoO_pushVFString, since that can also call cosmoV_error
    state->panic = true;

    // format the error string and push it onto the stack
    va_list args;
    va_start(args, format);
    cosmoO_pushVFString(state, format, args);
    va_end(args);

    // throw the error onto the state
    cosmoV_throw(state);
}

CObjUpval *captureUpvalue(CState *state, CValue *local)
{
    CObjUpval *prev = NULL;
    CObjUpval *upvalue = state->openUpvalues;

    while (upvalue != NULL &&
           upvalue->val > local) { // while upvalue exists and is higher on the stack than local
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->val == local) { // we found the local we were going to capture
        return upvalue;
    }

    CObjUpval *newUpval = cosmoO_newUpvalue(state, local);
    newUpval->next = upvalue;

    // the list is sorted, so insert it at our found upvalue
    if (prev == NULL) {
        state->openUpvalues = newUpval;
    } else {
        prev->next = newUpval;
    }

    return newUpval;
}

void closeUpvalues(CState *state, CValue *local)
{
    while (state->openUpvalues != NULL &&
           state->openUpvalues->val >=
               local) { // for every upvalue that points to the local or anything above it
        CObjUpval *upvalue = state->openUpvalues;
        upvalue->closed = *upvalue->val;
        upvalue->val = &upvalue->closed; // upvalue now points to itself :P
        state->openUpvalues = upvalue->next;
    }
}

void pushCallFrame(CState *state, CObjClosure *closure, int args)
{
#ifdef SAFE_STACK
    if (state->frameCount >= FRAME_MAX) {
        cosmoV_error(state, "Callframe overflow!");
        return;
    }
#endif

    CCallFrame *frame = &state->callFrame[state->frameCount++];
    frame->base = state->top - args - 1; // - 1 for the function
    frame->pc = closure->function->chunk.buf;
    frame->closure = closure;
}

// offset is the offset of the callframe base we set the state->top back too (useful for passing
// values in the stack as arguments, like methods)
void popCallFrame(CState *state, int offset)
{
    closeUpvalues(state,
                  state->callFrame[state->frameCount - 1].base); // close any upvalue still open

    state->top = state->callFrame[state->frameCount - 1].base + offset; // resets the stack
    state->frameCount--;
}

void cosmoV_concat(CState *state, int vals)
{
    StkPtr start = state->top - vals;
    StkPtr end = cosmoV_getTop(state, 0);

    CObjString *result = cosmoV_toString(state, *start);
    for (StkPtr current = start + 1; current <= end; current++) {
        cosmoV_pushRef(state, (CObj *)result); // so our GC can find our current result string
        CObjString *otherStr = cosmoV_toString(state, *current);
        cosmoV_pushRef(state, (CObj *)otherStr); // also so our GC won't free otherStr

        // concat the two strings together
        size_t sz = result->length + otherStr->length;
        char *buf = cosmoM_xmalloc(state, sz + 1); // +1 for null terminator

        memcpy(buf, result->str, result->length);
        memcpy(buf + result->length, otherStr->str, otherStr->length);
        buf[sz] = '\0';
        result = cosmoO_takeString(state, buf, sz);

        cosmoV_setTop(state, 2); // pop result & otherStr off the stack
    }

    state->top = start;
    cosmoV_pushRef(state, (CObj *)result);
}

int cosmoV_execute(CState *state);
bool invokeMethod(CState *state, CObj *obj, CValue func, int args, int nresults, int offset);

/*
    calls a native C Function with # args on the stack, nresults are pushed onto the stack upon
   return.

    returns:
        false: state paniced during C Function, error is at state->error
        true: state->top is moved to base + offset + nresults, with nresults pushed onto the stack
   from base + offset
*/
static bool callCFunction(CState *state, CosmoCFunction cfunc, int args, int nresults, int offset)
{
    StkPtr savedBase = cosmoV_getTop(state, args);

    int nres = cfunc(state, args, savedBase + 1);

    // caller function wasn't expecting this many return values, cap it
    if (nres > nresults)
        nres = nresults;

    // remember where the return values are
    StkPtr results = cosmoV_getTop(state, nres - 1);

    state->top = savedBase + offset; // set stack

    // if the state paniced during the c function, return false
    if (state->panic)
        return false;

    // push the return value back onto the stack
    memmove(state->top, results,
            sizeof(CValue) * nres); // copies the return values to the top of the stack
    state->top += nres;             // and make sure to move state->top to match

    // now, if the caller function expected more return values, push nils onto the stack
    for (int i = nres; i < nresults; i++)
        cosmoV_pushValue(state, cosmoV_newNil());

    return true;
}

/*
    calls a raw closure object with # args on the stack, nresults are pushed onto the stack upon
   return.

    returns:
        false: state paniced, error is at state->error
        true: stack->top is moved to base + offset + nresults, with nresults pushed onto the stack
   from base + offset
*/
static bool rawCall(CState *state, CObjClosure *closure, int args, int nresults, int offset)
{
    CObjFunction *func = closure->function;

    // if the function is variadic and theres more args than parameters, push the args into a table
    if (func->variadic && args >= func->args) {
        int extraArgs = args - func->args;
        StkPtr variStart = cosmoV_getTop(state, extraArgs - 1);

        // push key & value pairs
        for (int i = 0; i < extraArgs; i++) {
            cosmoV_pushNumber(state, i);
            cosmoV_pushValue(state, *(variStart + i));
        }

        cosmoV_makeTable(state, extraArgs);
        *variStart = *cosmoV_getTop(state, 0); // move table on the stack to the vari local
        state->top -= extraArgs;

        pushCallFrame(state, closure, func->args + 1);
    } else if (args != func->args) { // mismatched args
        cosmoV_error(state, "Expected %d arguments for %s, got %d!", closure->function->args,
                     closure->function->name == NULL ? UNNAMEDCHUNK : closure->function->name->str,
                     args);
        return false;
    } else {
        // load function into callframe
        pushCallFrame(state, closure, func->args);
    }

    // execute
    int nres = cosmoV_execute(state);

    if (nres > nresults) // caller function wasn't expecting this many return values, cap it
        nres = nresults;

    // remember where the return values are
    StkPtr results = cosmoV_getTop(state, nres - 1);

    // pop the callframe and return results :)
    popCallFrame(state, offset);

    if (state->panic) // panic state
        return false;

    // push the return values back onto the stack
    for (int i = 0; i < nres; i++) {
        state->top[i] = results[i];
    }
    state->top += nres; // and make sure to move state->top to match

    // now, if the caller function expected more return values, push nils onto the stack
    for (int i = nres; i < nresults; i++)
        cosmoV_pushValue(state, cosmoV_newNil());

    return true;
}

// returns true if successful, false if error
bool callCValue(CState *state, CValue func, int args, int nresults, int offset)
{
#ifdef VM_DEBUG
    printf("\n");
    printIndent(state->frameCount - 1);
    printValue(func);
    printf("(%d args)\n", args);
#endif

    if (!IS_REF(func)) {
        cosmoV_error(state, "Cannot call non-callable type %s!", cosmoV_typeStr(func));
        return false;
    }

    switch (cosmoV_readRef(func)->type) {
    case COBJ_CLOSURE:
        return rawCall(state, cosmoV_readClosure(func), args, nresults, offset);
    case COBJ_CFUNCTION:
        return callCFunction(state, cosmoV_readCFunction(func), args, nresults, offset);
    case COBJ_METHOD: {
        CObjMethod *method = (CObjMethod *)cosmoV_readRef(func);
        return invokeMethod(state, method->obj, method->func, args, nresults, offset + 1);
    }
    case COBJ_OBJECT: { // object is being instantiated, making another object
        CObjObject *protoObj = (CObjObject *)cosmoV_readRef(func);
        CValue ret;

        cosmoV_pushRef(state, (CObj *)protoObj); // push proto to stack for GC to find
        CObjObject *newObj = cosmoO_newObject(state);
        newObj->_obj.proto = protoObj;
        cosmoV_pop(state); // pop proto

        // check if they defined an initializer (we accept 0 return values)
        if (cosmoO_getIString(state, protoObj, ISTRING_INIT, &ret)) {
            if (!invokeMethod(state, (CObj *)newObj, ret, args, 0, offset + 1))
                return false;
        } else {
            // no default initializer
            cosmoV_error(state, "Expected __init() in proto, object cannot be instantiated!");
            return false;
        }

        if (nresults > 0) {
            cosmoV_pushRef(state, (CObj *)newObj);

            // push the nils to fill up the expected return values
            for (int i = 0; i < nresults - 1;
                 i++) { // -1 since the we already pushed the important value
                cosmoV_pushValue(state, cosmoV_newNil());
            }
        }
        break;
    }
    default:
        cosmoV_error(state, "Cannot call non-callable type %s!", cosmoV_typeStr(func));
        return false;
    }

    return true;
}

bool invokeMethod(CState *state, CObj *obj, CValue func, int args, int nresults, int offset)
{
    // first, set the first argument to the object
    StkPtr temp = cosmoV_getTop(state, args);
    *temp = cosmoV_newRef(obj);

    return callCValue(state, func, args + 1, nresults, offset);
}

// wraps cosmoV_call in a protected state, CObjError will be pushed onto the stack if function call
// failed, else return values are passed
// returns false if function call failed, true if function call succeeded
bool cosmoV_pcall(CState *state, int args, int nresults)
{
    StkPtr base = cosmoV_getTop(state, args);

    if (!callCValue(state, *base, args, nresults, 0)) {
        // restore panic state
        state->panic = false;

        if (nresults > 0) {
            cosmoV_pushRef(state, (CObj *)state->error);

            // push other expected results onto the stack
            for (int i = 0; i < nresults - 1; i++)
                cosmoV_pushValue(state, cosmoV_newNil());
        }

        return false;
    }

    return true;
}

// calls a callable object at stack->top - args - 1, passing the # of args to the callable, and
// ensuring nresults are returned
// returns false if an error was thrown, else true if successful
bool cosmoV_call(CState *state, int args, int nresults)
{
    StkPtr val = cosmoV_getTop(state, args); // function will always be right above the args

    return callCValue(state, *val, args, nresults, 0);
}

static inline bool isFalsey(StkPtr val)
{
    return IS_NIL(*val) || (IS_BOOLEAN(*val) && !cosmoV_readBoolean(*val));
}

COSMO_API CObjObject *cosmoV_makeObject(CState *state, int pairs)
{
    StkPtr key, val;
    CObjObject *newObj = cosmoO_newObject(state);
    cosmoV_pushRef(state, (CObj *)newObj); // so our GC doesn't free our new object

    for (int i = 0; i < pairs; i++) {
        val = cosmoV_getTop(state, (i * 2) + 1);
        key = cosmoV_getTop(state, (i * 2) + 2);

        // set key/value pair
        CValue *newVal = cosmoT_insert(state, &newObj->tbl, *key);
        *newVal = *val;
    }

    // once done, pop everything off the stack + push new object
    cosmoV_setTop(state, (pairs * 2) + 1); // + 1 for our object
    cosmoV_pushRef(state, (CObj *)newObj);
    return newObj;
}

COSMO_API bool cosmoV_registerProtoObject(CState *state, CObjType objType, CObjObject *obj)
{
    bool replaced = state->protoObjects[objType] != NULL;
    state->protoObjects[objType] = obj;

    // walk through the object list
    CObj *curr = state->objects;
    while (curr != NULL) {
        // update the proto
        if (curr->type == objType && curr->proto != NULL) {
            curr->proto = obj;
        }
        curr = curr->next;
    }

    return replaced;
}

COSMO_API void cosmoV_makeTable(CState *state, int pairs)
{
    StkPtr key, val;
    CObjTable *newObj = cosmoO_newTable(state);
    cosmoV_pushRef(state, (CObj *)newObj); // so our GC doesn't free our new table

    for (int i = 0; i < pairs; i++) {
        val = cosmoV_getTop(state, (i * 2) + 1);
        key = cosmoV_getTop(state, (i * 2) + 2);

        // set key/value pair
        CValue *newVal = cosmoT_insert(state, &newObj->tbl, *key);
        *newVal = *val;
    }

    // once done, pop everything off the stack + push new table
    cosmoV_setTop(state, (pairs * 2) + 1); // + 1 for our table
    cosmoV_pushRef(state, (CObj *)newObj);
}

bool cosmoV_rawget(CState *state, CObj *_obj, CValue key, CValue *val)
{
    CObjObject *object = cosmoO_grabProto(_obj);

    // no proto to get from
    if (object == NULL) {
        CObjString *field = cosmoV_toString(state, key);
        cosmoV_error(state, "No proto defined! Couldn't get field '%s' from type %s", field->str,
                     cosmoO_typeStr(_obj));
        *val = cosmoV_newNil();
        return false;
    }

    // push the object onto the stack so the GC can find it
    cosmoV_pushRef(state, (CObj *)object);
    if (cosmoO_getRawObject(state, object, key, val, _obj)) {
        // *val now equals the response, pop the object
        cosmoV_pop(state);
        return true;
    }

    cosmoV_pop(state);
    return false;
}

bool cosmoV_rawset(CState *state, CObj *_obj, CValue key, CValue val)
{
    CObjObject *object = cosmoO_grabProto(_obj);

    // no proto to set to
    if (object == NULL) {
        CObjString *field = cosmoV_toString(state, key);
        cosmoV_error(state, "No proto defined! Couldn't set field '%s' to type %s", field->str,
                     cosmoO_typeStr(_obj));
        return false;
    }

    cosmoO_setRawObject(state, object, key, val, _obj);
    return true;
}

COSMO_API bool cosmoV_get(CState *state)
{
    CValue val;
    StkPtr obj = cosmoV_getTop(state, 1); // object was pushed first
    StkPtr key = cosmoV_getTop(state, 0); // then the key

    if (!IS_REF(*obj)) {
        cosmoV_error(state, "Couldn't get field from type %s!", cosmoV_typeStr(*obj));
        return false;
    }

    if (!cosmoV_rawget(state, cosmoV_readRef(*obj), *key, &val))
        return false;

    // pop the obj & key, push the value
    cosmoV_setTop(state, 2);
    cosmoV_pushValue(state, val);
    return true;
}

// yes, this would technically make it possible to set fields of types other than <string>. go crazy
COSMO_API bool cosmoV_set(CState *state)
{
    StkPtr obj = cosmoV_getTop(state, 2); // object was pushed first
    StkPtr key = cosmoV_getTop(state, 1); // then the key
    StkPtr val = cosmoV_getTop(state, 0); // and finally the value

    if (!IS_REF(*obj)) {
        cosmoV_error(state, "Couldn't set field on type %s!", cosmoV_typeStr(*obj));
        return false;
    }

    if (!cosmoV_rawset(state, cosmoV_readRef(*obj), *key, *val))
        return false;

    // pop the obj, key & value
    cosmoV_setTop(state, 3);
    return true;
}

COSMO_API bool cosmoV_getMethod(CState *state, CObj *obj, CValue key, CValue *val)
{
    if (!cosmoV_rawget(state, obj, key, val))
        return false;

    // if the result is callable, wrap it in an method
    if (IS_CALLABLE(*val)) {
        // push object to stack so the GC can find it
        cosmoV_pushRef(state, (CObj *)obj);
        CObjMethod *method = cosmoO_newMethod(state, *val, obj);
        cosmoV_pop(state); // pop the object
        *val = cosmoV_newRef(method);
    }

    return true;
}

int _tbl__next(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 parameter, %d received!", nargs);
        return 0;
    }

    if (!IS_OBJECT(args[0])) {
        cosmoV_error(state, "Expected iterable object, %s received!", cosmoV_typeStr(args[0]));
        return 0;
    }

    CObjObject *obj = cosmoV_readObject(args[0]);
    int index = cosmoO_getUserI(obj); // we store the index in the userdata
    CValue val;

    cosmoO_getIString(state, obj, ISTRING_RESERVED, &val);

    if (!IS_TABLE(val)) {
        return 0; // someone set the __reserved member to something else. this will exit the
                  // iterator loop
    }

    CObjTable *table = (CObjTable *)cosmoV_readRef(val);

    // while the entry is invalid, go to the next entry
    int cap = cosmoT_getCapacity(&table->tbl);
    CTableEntry *entry;
    do {
        entry = &table->tbl.table[index++];
    } while (IS_NIL(entry->key) && index < cap);
    cosmoO_setUserI(obj, index); // update the userdata

    if (index < cap &&
        !IS_NIL(entry->key)) { // if the entry is valid, return it's key and value pair
        cosmoV_pushValue(state, entry->key);
        cosmoV_pushValue(state, entry->val);
        return 2; // we pushed 2 values onto the stack for the return values
    } else {
        return 0; // we have nothing to return, this should exit the iterator loop
    }
}

#define NUMBEROP(typeConst, op)                                                                    \
    StkPtr valA = cosmoV_getTop(state, 1);                                                         \
    StkPtr valB = cosmoV_getTop(state, 0);                                                         \
    if (IS_NUMBER(*valA) && IS_NUMBER(*valB)) {                                                    \
        cosmoV_setTop(state, 2); /* pop the 2 values */                                            \
        cosmoV_pushValue(state, typeConst(cosmoV_readNumber(*valA) op cosmoV_readNumber(*valB)));  \
    } else {                                                                                       \
        cosmoV_error(state, "Expected numbers, got %s and %s!", cosmoV_typeStr(*valA),             \
                     cosmoV_typeStr(*valB));                                                       \
        return -1;                                                                                 \
    }

static inline uint8_t READBYTE(CCallFrame *frame)
{
    return *frame->pc++;
}

static inline uint16_t READUINT(CCallFrame *frame)
{
    frame->pc += 2;
    return *(uint16_t *)(&frame->pc[-2]);
}

#ifdef VM_JUMPTABLE
#    define DISPATCH goto *cosmoV_dispatchTable[READBYTE(frame)]
#    define CASE(op)                                                                               \
        DISPATCH;                                                                                  \
        JMP_##op
#    define JMPLABEL(op) &&JMP_##op
#    define SWITCH                                                                                 \
        static void *cosmoV_dispatchTable[] = {                                                    \
            JMPLABEL(OP_LOADCONST),     JMPLABEL(OP_SETGLOBAL), JMPLABEL(OP_GETGLOBAL),            \
            JMPLABEL(OP_SETLOCAL),      JMPLABEL(OP_GETLOCAL),  JMPLABEL(OP_GETUPVAL),             \
            JMPLABEL(OP_SETUPVAL),      JMPLABEL(OP_PEJMP),     JMPLABEL(OP_EJMP),                 \
            JMPLABEL(OP_JMP),           JMPLABEL(OP_JMPBACK),   JMPLABEL(OP_POP),                  \
            JMPLABEL(OP_CALL),          JMPLABEL(OP_CLOSURE),   JMPLABEL(OP_CLOSE),                \
            JMPLABEL(OP_NEWTABLE),      JMPLABEL(OP_NEWARRAY),  JMPLABEL(OP_INDEX),                \
            JMPLABEL(OP_NEWINDEX),      JMPLABEL(OP_NEWOBJECT), JMPLABEL(OP_SETOBJECT),            \
            JMPLABEL(OP_GETOBJECT),     JMPLABEL(OP_GETMETHOD), JMPLABEL(OP_INVOKE),               \
            JMPLABEL(OP_ITER),          JMPLABEL(OP_NEXT),      JMPLABEL(OP_ADD),                  \
            JMPLABEL(OP_SUB),           JMPLABEL(OP_MULT),      JMPLABEL(OP_DIV),                  \
            JMPLABEL(OP_MOD),           JMPLABEL(OP_POW),       JMPLABEL(OP_NOT),                  \
            JMPLABEL(OP_NEGATE),        JMPLABEL(OP_COUNT),     JMPLABEL(OP_CONCAT),               \
            JMPLABEL(OP_INCLOCAL),      JMPLABEL(OP_INCGLOBAL), JMPLABEL(OP_INCUPVAL),             \
            JMPLABEL(OP_INCINDEX),      JMPLABEL(OP_INCOBJECT), JMPLABEL(OP_EQUAL),                \
            JMPLABEL(OP_LESS),          JMPLABEL(OP_GREATER),   JMPLABEL(OP_LESS_EQUAL),           \
            JMPLABEL(OP_GREATER_EQUAL), JMPLABEL(OP_TRUE),      JMPLABEL(OP_FALSE),                \
            JMPLABEL(OP_NIL),           JMPLABEL(OP_RETURN),                                       \
        };                                                                                         \
        DISPATCH;
#    define DEFAULT DISPATCH /* no-op */
#else
#    define CASE(op)                                                                               \
        continue;                                                                                  \
    case op
#    define SWITCH switch (READBYTE(frame))
#    define DEFAULT                                                                                \
    default:                                                                                       \
        CERROR("unknown opcode!");                                                                 \
        exit(0)
#endif

// returns -1 if panic
int cosmoV_execute(CState *state)
{
    CCallFrame *frame = &state->callFrame[state->frameCount - 1];         // grabs the current frame
    CValue *constants = frame->closure->function->chunk.constants.values; // cache the pointer :)

    while (!state->panic) {
#ifdef VM_DEBUG
        cosmoV_printStack(state);
        disasmInstr(&frame->closure->function->chunk,
                    frame->pc - frame->closure->function->chunk.buf, state->frameCount - 1);
        printf("\n");
#endif
        SWITCH
        {
            CASE(OP_LOADCONST) :
            { // push const[uint] to stack
                uint16_t indx = READUINT(frame);
                cosmoV_pushValue(state, constants[indx]);
            }
            CASE(OP_SETGLOBAL) :
            {
                uint16_t indx = READUINT(frame);
                CValue ident = constants[indx]; // grabs identifier
                CValue *val = cosmoT_insert(state, &state->globals->tbl, ident);
                *val = *cosmoV_pop(state); // sets the value in the hash table
            }
            CASE(OP_GETGLOBAL) :
            {
                uint16_t indx = READUINT(frame);
                CValue ident = constants[indx]; // grabs identifier
                CValue val;                     // to hold our value
                cosmoT_get(state, &state->globals->tbl, ident, &val);
                cosmoV_pushValue(state, val); // pushes the value to the stack
            }
            CASE(OP_SETLOCAL) :
            {
                uint8_t indx = READBYTE(frame);
                // set base to top of stack & pop
                frame->base[indx] = *cosmoV_pop(state);
            }
            CASE(OP_GETLOCAL) :
            {
                uint8_t indx = READBYTE(frame);
                cosmoV_pushValue(state, frame->base[indx]);
                continue;
            }
            CASE(OP_GETUPVAL) :
            {
                uint8_t indx = READBYTE(frame);
                cosmoV_pushValue(state, *frame->closure->upvalues[indx]->val);
            }
            CASE(OP_SETUPVAL) :
            {
                uint8_t indx = READBYTE(frame);
                *frame->closure->upvalues[indx]->val = *cosmoV_pop(state);
            }
            CASE(OP_PEJMP) :
            { // pop equality jump
                uint16_t offset = READUINT(frame);

                if (isFalsey(cosmoV_pop(state))) { // pop, if the condition is false, jump!
                    frame->pc += offset;
                }
            }
            CASE(OP_EJMP) :
            { // equality jump
                uint16_t offset = READUINT(frame);

                if (isFalsey(cosmoV_getTop(state, 0))) { // if the condition is false, jump!
                    frame->pc += offset;
                }
            }
            CASE(OP_JMP) :
            { // jump
                uint16_t offset = READUINT(frame);
                frame->pc += offset;
            }
            CASE(OP_JMPBACK) :
            {
                uint16_t offset = READUINT(frame);
                frame->pc -= offset;
            }
            CASE(OP_POP) :
            { // pops value off the stack
                cosmoV_setTop(state, READBYTE(frame));
            }
            CASE(OP_CALL) :
            {
                uint8_t args = READBYTE(frame);
                uint8_t nres = READBYTE(frame);
                if (!cosmoV_call(state, args, nres)) {
                    return -1;
                }
            }
            CASE(OP_CLOSURE) :
            {
                uint16_t index = READUINT(frame);
                CObjFunction *func = cosmoV_readFunction(constants[index]);
                CObjClosure *closure = cosmoO_newClosure(state, func);
                cosmoV_pushRef(state, (CObj *)closure);

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t encoding = READBYTE(frame);
                    uint8_t index = READBYTE(frame);
                    if (encoding == OP_GETUPVAL) {
                        // capture upvalue from current frame's closure
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    } else {
                        // capture local
                        closure->upvalues[i] = captureUpvalue(state, frame->base + index);
                    }
                }
            }
            CASE(OP_CLOSE) :
            {
                closeUpvalues(state, state->top - 1);
                cosmoV_pop(state);
            }
            CASE(OP_NEWTABLE) :
            {
                uint16_t pairs = READUINT(frame);
                cosmoV_makeTable(state, pairs);
            }
            CASE(OP_NEWARRAY) :
            {
                uint16_t pairs = READUINT(frame);
                StkPtr val;
                CObjTable *newObj = cosmoO_newTable(state);
                cosmoV_pushRef(state, (CObj *)newObj); // so our GC doesn't free our new table

                for (int i = 0; i < pairs; i++) {
                    val = cosmoV_getTop(state, i + 1);

                    // set key/value pair
                    CValue *newVal =
                        cosmoT_insert(state, &newObj->tbl, cosmoV_newNumber(pairs - i - 1));
                    *newVal = *val;
                }

                // once done, pop everything off the stack + push new table
                cosmoV_setTop(state, pairs + 1); // + 1 for our table
                cosmoV_pushRef(state, (CObj *)newObj);
            }
            CASE(OP_INDEX) :
            {
                StkPtr key = cosmoV_getTop(state, 0);  // key should be the top of the stack
                StkPtr temp = cosmoV_getTop(state, 1); // after that should be the table

                // sanity check
                if (!IS_REF(*temp)) {
                    cosmoV_error(state, "Couldn't index type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                CObj *obj = cosmoV_readRef(*temp);
                CObjObject *proto = cosmoO_grabProto(obj);
                CValue val; // to hold our value

                if (proto != NULL) {
                    // check for __index metamethod
                    if (!cosmoO_indexObject(state, proto, *key,
                                            &val)) // if returns false, cosmoV_error was called
                        return -1;
                } else if (obj->type == COBJ_TABLE) {
                    CObjTable *tbl = (CObjTable *)obj;

                    cosmoT_get(state, &tbl->tbl, *key, &val);
                } else {
                    cosmoV_error(state, "No proto defined! Couldn't __index from type %s",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_setTop(state, 2);      // pops the table & the key
                cosmoV_pushValue(state, val); // pushes the field result
            }
            CASE(OP_NEWINDEX) :
            {
                StkPtr value = cosmoV_getTop(state, 0); // value is at the top of the stack
                StkPtr key = cosmoV_getTop(state, 1);
                StkPtr temp = cosmoV_getTop(state, 2); // table is after the key

                // sanity check
                if (!IS_REF(*temp)) {
                    cosmoV_error(state, "Couldn't set index with type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                CObj *obj = cosmoV_readRef(*temp);
                CObjObject *proto = cosmoO_grabProto(obj);

                if (proto != NULL) {
                    if (!cosmoO_newIndexObject(
                            state, proto, *key,
                            *value)) // if it returns false, cosmoV_error was called
                        return -1;
                } else if (obj->type == COBJ_TABLE) {
                    CObjTable *tbl = (CObjTable *)obj;
                    CValue *newVal = cosmoT_insert(state, &tbl->tbl, *key);

                    *newVal = *value; // set the index
                } else {
                    cosmoV_error(state, "No proto defined! Couldn't __newindex from type %s",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                // pop everything off the stack
                cosmoV_setTop(state, 3);
            }
            CASE(OP_NEWOBJECT) :
            {
                uint16_t pairs = READUINT(frame);
                cosmoV_makeObject(state, pairs);
            }
            CASE(OP_SETOBJECT) :
            {
                StkPtr value = cosmoV_getTop(state, 0); // value is at the top of the stack
                StkPtr temp = cosmoV_getTop(state, 1);  // object is after the value
                uint16_t ident = READUINT(frame);       // use for the key

                // sanity check
                if (IS_REF(*temp)) {
                    if (!cosmoV_rawset(state, cosmoV_readRef(*temp), constants[ident], *value))
                        return -1;
                } else {
                    CObjString *field = cosmoV_toString(state, constants[ident]);
                    cosmoV_error(state, "Couldn't set field '%s' on type %s!", field->str,
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                // pop everything off the stack
                cosmoV_setTop(state, 2);
            }
            CASE(OP_GETOBJECT) :
            {
                CValue val;                            // to hold our value
                StkPtr temp = cosmoV_getTop(state, 0); // that should be the object
                uint16_t ident = READUINT(frame);      // use for the key

                // sanity check
                if (IS_REF(*temp)) {
                    if (!cosmoV_rawget(state, cosmoV_readRef(*temp), constants[ident], &val))
                        return -1;
                } else {
                    CObjString *field = cosmoV_toString(state, constants[ident]);
                    cosmoV_error(state, "Couldn't get field '%s' from type %s!", field->str,
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_setTop(state, 1);      // pops the object
                cosmoV_pushValue(state, val); // pushes the field result
            }
            CASE(OP_GETMETHOD) :
            {
                CValue val;                            // to hold our value
                StkPtr temp = cosmoV_getTop(state, 0); // that should be the object
                uint16_t ident = READUINT(frame);      // use for the key

                // this is almost identical to GETOBJECT, however cosmoV_getMethod is used instead
                // of just cosmoV_get
                if (IS_REF(*temp)) {
                    if (!cosmoV_getMethod(state, cosmoV_readRef(*temp), constants[ident], &val))
                        return -1;
                } else {
                    CObjString *field = cosmoV_toString(state, constants[ident]);
                    cosmoV_error(state, "Couldn't get field '%s' from type %s!", field->str,
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_setTop(state, 1);      // pops the object
                cosmoV_pushValue(state, val); // pushes the field result
            }
            CASE(OP_INVOKE) :
            {
                uint8_t args = READBYTE(frame);
                uint8_t nres = READBYTE(frame);
                uint16_t ident = READUINT(frame);
                StkPtr temp = cosmoV_getTop(state, args); // grabs object from stack
                CValue val;                               // to hold our value

                // sanity check
                if (IS_REF(*temp)) {
                    // get the field from the object
                    if (!cosmoV_rawget(state, cosmoV_readRef(*temp), constants[ident], &val))
                        return -1;

                    // now invoke the method!
                    invokeMethod(state, cosmoV_readRef(*temp), val, args, nres, 1);
                } else {
                    cosmoV_error(state, "Couldn't get from type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }
            }
            CASE(OP_ITER) :
            {
                StkPtr temp = cosmoV_getTop(state, 0); // should be the object/table

                if (!IS_REF(*temp)) {
                    cosmoV_error(state, "Couldn't iterate over non-iterator type %s!",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                CObj *obj = cosmoV_readRef(*temp);
                CObjObject *proto = cosmoO_grabProto(obj);
                CValue val;

                if (proto != NULL) {
                    // grab __iter & call it
                    if (cosmoO_getIString(state, proto, ISTRING_ITER, &val)) {
                        cosmoV_pop(state); // pop the object from the stack
                        cosmoV_pushValue(state, val);
                        cosmoV_pushRef(state, (CObj *)obj);
                        if (!cosmoV_call(
                                state, 1,
                                1)) // we expect 1 return value on the stack, the iterable object
                            return -1;

                        StkPtr iObj = cosmoV_getTop(state, 0);

                        if (!IS_OBJECT(*iObj)) {
                            cosmoV_error(state,
                                         "Expected iterable object! '__iter' returned %s, expected "
                                         "<object>!",
                                         cosmoV_typeStr(*iObj));
                            return -1;
                        }

                        // get __next method and place it at the top of the stack
                        cosmoV_getMethod(state, cosmoV_readRef(*iObj),
                                         cosmoV_newRef(state->iStrings[ISTRING_NEXT]), iObj);
                    } else {
                        cosmoV_error(state, "Expected iterable object! '__iter' not defined!");
                        return -1;
                    }
                } else if (obj->type == COBJ_TABLE) {
                    CObjTable *tbl = (CObjTable *)obj;

                    cosmoV_pushRef(state, (CObj *)state->iStrings[ISTRING_RESERVED]); // key
                    cosmoV_pushRef(state, (CObj *)tbl);                               // value

                    cosmoV_pushString(state, "__next"); // key
                    CObjCFunction *tbl_next = cosmoO_newCFunction(state, _tbl__next);
                    cosmoV_pushRef(state, (CObj *)tbl_next); // value

                    CObjObject *obj =
                        cosmoV_makeObject(state, 2); // pushes the new object to the stack
                    cosmoO_setUserI(obj, 0);         // increment for iterator

                    // make our CObjMethod for OP_NEXT to call
                    CObjMethod *method =
                        cosmoO_newMethod(state, cosmoV_newRef(tbl_next), (CObj *)obj);

                    cosmoV_setTop(state, 2);               // pops the object & the tbl
                    cosmoV_pushRef(state, (CObj *)method); // pushes the method for OP_NEXT
                } else {
                    cosmoV_error(state, "No proto defined! Couldn't get from type %s",
                                 cosmoO_typeStr(obj));
                    return -1;
                }
            }
            CASE(OP_NEXT) :
            {
                uint8_t nresults = READBYTE(frame);
                uint16_t jump = READUINT(frame);
                StkPtr temp = cosmoV_getTop(state, 0); // we don't actually pop this off the stack

                if (!IS_METHOD(*temp)) {
                    cosmoV_error(state, "Expected '__next' to be a method, got type %s!",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_pushValue(state, *temp);
                if (!cosmoV_call(state, 0, nresults))
                    return -1;

                if (IS_NIL(*(cosmoV_getTop(
                        state, 0)))) { // __next returned a nil, which means to exit the loop
                    cosmoV_setTop(state, nresults); // pop the return values
                    frame->pc += jump;
                }
            }
            CASE(OP_ADD) :
            {
                // pop 2 values off the stack & try to add them together
                NUMBEROP(cosmoV_newNumber, +);
            }
            CASE(OP_SUB) :
            {
                // pop 2 values off the stack & try to subtracts them
                NUMBEROP(cosmoV_newNumber, -);
            }
            CASE(OP_MULT) :
            {
                // pop 2 values off the stack & try to multiplies them together
                NUMBEROP(cosmoV_newNumber, *);
            }
            CASE(OP_DIV) :
            {
                // pop 2 values off the stack & try to divides them
                NUMBEROP(cosmoV_newNumber, /);
            }
            CASE(OP_MOD) :
            {
                StkPtr valA = cosmoV_getTop(state, 1);
                StkPtr valB = cosmoV_getTop(state, 0);
                if (IS_NUMBER(*valA) && IS_NUMBER(*valB)) {
                    cosmoV_setTop(state, 2); /* pop the 2 values */
                    cosmoV_pushValue(state, cosmoV_newNumber(fmod(cosmoV_readNumber(*valA),
                                                                  cosmoV_readNumber(*valB))));
                } else {
                    cosmoV_error(state, "Expected numbers, got %s and %s!", cosmoV_typeStr(*valA),
                                 cosmoV_typeStr(*valB));
                    return -1;
                }
            }
            CASE(OP_POW) :
            {
                StkPtr valA = cosmoV_getTop(state, 1);
                StkPtr valB = cosmoV_getTop(state, 0);
                if (IS_NUMBER(*valA) && IS_NUMBER(*valB)) {
                    cosmoV_setTop(state, 2); /* pop the 2 values */
                    cosmoV_pushValue(state, cosmoV_newNumber(pow(cosmoV_readNumber(*valA),
                                                                 cosmoV_readNumber(*valB))));
                } else {
                    cosmoV_error(state, "Expected numbers, got %s and %s!", cosmoV_typeStr(*valA),
                                 cosmoV_typeStr(*valB));
                    return -1;
                }
            }
            CASE(OP_NOT) :
            {
                cosmoV_pushBoolean(state, isFalsey(cosmoV_pop(state)));
            }
            CASE(OP_NEGATE) :
            { // pop 1 value off the stack & try to negate
                StkPtr val = cosmoV_getTop(state, 0);

                if (IS_NUMBER(*val)) {
                    cosmoV_pop(state);
                    cosmoV_pushNumber(state, -(cosmoV_readNumber(*val)));
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }
            }
            CASE(OP_COUNT) :
            {
                StkPtr temp = cosmoV_getTop(state, 0);

                if (!IS_REF(*temp)) {
                    cosmoV_error(state, "Expected non-primitive, got %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                int count = cosmoO_count(state, cosmoV_readRef(*temp));
                cosmoV_pop(state);

                cosmoV_pushNumber(state, count); // pushes the count onto the stack
            }
            CASE(OP_CONCAT) :
            {
                uint8_t vals = READBYTE(frame);
                cosmoV_concat(state, vals);
            }
            CASE(OP_INCLOCAL) :
            {                                       // this leaves the value on the stack
                int8_t inc = READBYTE(frame) - 128; // amount we're incrementing by
                uint8_t indx = READBYTE(frame);
                StkPtr val = &frame->base[indx];

                // check that it's a number value
                if (IS_NUMBER(*val)) {
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }
            }
            CASE(OP_INCGLOBAL) :
            {
                int8_t inc = READBYTE(frame) - 128; // amount we're incrementing by
                uint16_t indx = READUINT(frame);
                CValue ident = constants[indx]; // grabs identifier
                CValue *val = cosmoT_insert(state, &state->globals->tbl, ident);

                // check that it's a number value
                if (IS_NUMBER(*val)) {
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }
            }
            CASE(OP_INCUPVAL) :
            {
                int8_t inc = READBYTE(frame) - 128; // amount we're incrementing by
                uint8_t indx = READBYTE(frame);
                CValue *val = frame->closure->upvalues[indx]->val;

                // check that it's a number value
                if (IS_NUMBER(*val)) {
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }
            }
            CASE(OP_INCINDEX) :
            {
                int8_t inc = READBYTE(frame) - 128;    // amount we're incrementing by
                StkPtr temp = cosmoV_getTop(state, 1); // object should be above the key
                StkPtr key = cosmoV_getTop(state, 0);  // grabs key

                if (!IS_REF(*temp)) {
                    cosmoV_error(state, "Couldn't index non-indexable type %s!",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }

                CObj *obj = cosmoV_readRef(*temp);
                CObjObject *proto = cosmoO_grabProto(obj);
                CValue val;

                // call __index if the proto was found
                if (proto != NULL) {
                    if (cosmoO_indexObject(state, proto, *key, &val)) {
                        if (!IS_NUMBER(val)) {
                            cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(val));
                            return -1;
                        }

                        cosmoV_pushValue(state, val); // pushes old value onto the stack :)

                        // call __newindex
                        if (!cosmoO_newIndexObject(state, proto, *key,
                                                   cosmoV_newNumber(cosmoV_readNumber(val) + inc)))
                            return -1;
                    } else
                        return -1; // cosmoO_indexObject failed and threw an error
                } else if (obj->type == COBJ_TABLE) {
                    CObjTable *tbl = (CObjTable *)obj;
                    CValue *val = cosmoT_insert(state, &tbl->tbl, *key);

                    if (!IS_NUMBER(*val)) {
                        cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                        return -1;
                    }

                    // pops tbl & key from stack
                    cosmoV_setTop(state, 2);
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc); // sets table index
                } else {
                    cosmoV_error(state, "No proto defined! Couldn't __index from type %s",
                                 cosmoV_typeStr(*temp));
                    return -1;
                }
            }
            CASE(OP_INCOBJECT) :
            {
                int8_t inc = READBYTE(frame) - 128; // amount we're incrementing by
                uint16_t indx = READUINT(frame);
                StkPtr temp = cosmoV_getTop(state, 0); // object should be at the top of the stack
                CValue ident = constants[indx];        // grabs identifier

                // sanity check
                if (IS_REF(*temp)) {
                    CObj *obj = cosmoV_readRef(*temp);
                    CValue val;

                    if (!cosmoV_rawget(state, obj, ident, &val))
                        return -1;

                    // pop the object off the stack
                    cosmoV_pop(state);

                    // check that it's a number value
                    if (IS_NUMBER(val)) {
                        cosmoV_pushValue(state, val); // pushes old value onto the stack :)
                        if (!cosmoV_rawset(state, obj, ident,
                                           cosmoV_newNumber(cosmoV_readNumber(val) + inc)))
                            return -1;
                    } else {
                        cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(val));
                        return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't set a field on type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }
            }
            CASE(OP_EQUAL) :
            {
                // pop vals
                StkPtr valB = cosmoV_pop(state);
                StkPtr valA = cosmoV_pop(state);

                // compare & push
                cosmoV_pushBoolean(state, cosmoV_equal(state, *valA, *valB));
            }
            CASE(OP_LESS) :
            {
                NUMBEROP(cosmoV_newBoolean, <);
            }
            CASE(OP_GREATER) :
            {
                NUMBEROP(cosmoV_newBoolean, >);
            }
            CASE(OP_LESS_EQUAL) :
            {
                NUMBEROP(cosmoV_newBoolean, <=);
            }
            CASE(OP_GREATER_EQUAL) :
            {
                NUMBEROP(cosmoV_newBoolean, >=);
            }
            CASE(OP_TRUE) : cosmoV_pushBoolean(state, true);
            CASE(OP_FALSE) : cosmoV_pushBoolean(state, false);
            CASE(OP_NIL) : cosmoV_pushValue(state, cosmoV_newNil());
            CASE(OP_RETURN) :
            {
                uint8_t res = READBYTE(frame);
                return res;
            }
            DEFAULT;
        }
    }

    // we'll only reach this if state->panic is true
    return -1;
}

#undef NUMBEROP
