#include "cvm.h"
#include "cstate.h"
#include "cdebug.h"
#include "cmem.h"

#include <stdarg.h>
#include <string.h>

#include <math.h>

COSMO_API void cosmoV_pushFString(CState *state, const char *format, ...) {
    va_list args;
    va_start(args, format);
    cosmoO_pushVFString(state, format, args);
    va_end(args);
}

void cosmoV_error(CState *state, const char *format, ...) {
    if (state->panic)
        return;
    state->panic = true;

    // print stack trace
    for (int i = 0; i < state->frameCount; i++) {
        CCallFrame *frame = &state->callFrame[i];
        CObjFunction *function = frame->closure->function;
        CChunk *chunk = &function->chunk;

        int line = chunk->lineInfo[frame->pc - chunk->buf - 1];

        if (i == state->frameCount - 1) { // it's the last call frame, prepare for the objection to be printed
            fprintf(stderr, "Objection in %.*s on [line %d] in ", function->module->length, function->module->str, line);
            if (function->name == NULL) { // unnamed chunk
                fprintf(stderr, "%s\n\t", UNNAMEDCHUNK);
            } else {
                fprintf(stderr, "%.*s()\n\t", function->name->length, function->name->str);
            }
        } else {
            fprintf(stderr, "[line %d] in ", line);
            if (function->name == NULL) { // unnamed chunk
                fprintf(stderr, "%s\n", UNNAMEDCHUNK);
            } else {
                fprintf(stderr, "%.*s()\n", function->name->length, function->name->str);
            }
        }
    }

    va_list args;
    va_start(args, format);
    CObjString *errString = cosmoO_pushVFString(state, format, args);
    va_end(args);

    printf("%.*s\n", errString->length, errString->str);
    //cosmoV_printStack(state);
}

CObjUpval *captureUpvalue(CState *state, CValue *local) {
    CObjUpval *prev = NULL;
    CObjUpval *upvalue = state->openUpvalues;

    while (upvalue != NULL && upvalue->val > local) { // while upvalue exists and is higher on the stack than local
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

void closeUpvalues(CState *state, CValue *local) {
    while (state->openUpvalues != NULL && state->openUpvalues->val >= local) { // for every upvalue that points to the local or anything above it
        CObjUpval *upvalue = state->openUpvalues;
        upvalue->closed = *upvalue->val;
        upvalue->val = &upvalue->closed; // upvalue now points to itself :P
        state->openUpvalues = upvalue->next;
    }
}

void pushCallFrame(CState *state, CObjClosure *closure, int args) {
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

// offset is the offset of the callframe base we set the state->top back too (useful for passing values in the stack as arguments, like methods)
void popCallFrame(CState *state, int offset) {
    closeUpvalues(state, state->callFrame[state->frameCount - 1].base); // close any upvalue still open

    state->top = state->callFrame[state->frameCount - 1].base + offset; // resets the stack
    state->frameCount--;
}

void cosmoV_concat(CState *state, int vals) {
    StkPtr start = state->top - vals;
    StkPtr end = cosmoV_getTop(state, 0);

    CObjString *result = cosmoV_toString(state, *start);
    for (StkPtr current = start + 1; current <= end; current++) {
        cosmoV_pushValue(state, cosmoV_newObj(result)); // so our GC can find our current result string
        CObjString *otherStr = cosmoV_toString(state, *current);
        cosmoV_pushValue(state, cosmoV_newObj(otherStr)); // also so our GC won't free otherStr

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
    cosmoV_pushValue(state, cosmoV_newObj(result));
}

int cosmoV_execute(CState *state);

static inline void callCFunction(CState *state, CosmoCFunction cfunc, int args, int nresults, int offset) {
    StkPtr savedBase = cosmoV_getTop(state, args);

    cosmoM_freezeGC(state); // we don't want a GC event during c api because we don't actually trust the user to know how to evade the GC
    int nres = cfunc(state, args, savedBase + 1);
    cosmoM_unfreezeGC(state);

    if (nres > nresults) // caller function wasn't expecting this many return values, cap it
        nres = nresults;

    // remember where the return values are
    StkPtr results = cosmoV_getTop(state, nres-1);

    state->top = savedBase + offset; // set stack

    // push the return value back onto the stack
    memmove(state->top, results, sizeof(CValue) * nres); // copies the return values to the top of the stack
    state->top += nres; // and make sure to move state->top to match

    // now, if the caller function expected more return values, push nils onto the stack
    for (int i = nres; i < nresults; i++)
        cosmoV_pushValue(state, cosmoV_newNil());
}

bool call(CState *state, CObjClosure *closure, int args, int nresults, int offset) {
    CObjFunction *func = closure->function;

    // if the function is variadic and theres more args than parameters, push the args into a dictionary
    if (func->variadic && args >= func->args) {
        int extraArgs = args - func->args;
        StkPtr variStart = cosmoV_getTop(state, extraArgs-1);

        // push key & value pairs
        for (int i = 0; i < extraArgs; i++) {
            cosmoV_pushNumber(state, i);
            cosmoV_pushValue(state, *(variStart + i));
        }

        cosmoV_makeDictionary(state, extraArgs);
        *variStart = *cosmoV_getTop(state, 0); // move dict on the stack to the vari local
        state->top -= extraArgs;

        pushCallFrame(state, closure, func->args + 1);
    } else if (args != func->args) { // mismatched args
        cosmoV_error(state, "Expected %d arguments for %s, got %d!", closure->function->args, closure->function->name == NULL ? UNNAMEDCHUNK : closure->function->name->str, args);
        return false;
    } else {
        // load function into callframe
        pushCallFrame(state, closure, func->args);
    }

    // execute
    int nres = cosmoV_execute(state);
    if (nres == -1) // panic state
        return false;

    if (nres > nresults) // caller function wasn't expecting this many return values, cap it
        nres = nresults;

    // remember where the return values are
    StkPtr results = cosmoV_getTop(state, nres-1);

    // pop the callframe and return results :)
    popCallFrame(state, offset);

    // push the return values back onto the stack
    memmove(state->top, results, sizeof(CValue) * nres); // copies the return values to the top of the stack
    state->top += nres; // and make sure to move state->top to match

    // now, if the caller function expected more return values, push nils onto the stack
    for (int i = nres; i < nresults; i++)
        cosmoV_pushValue(state, cosmoV_newNil());

    return true;
}

bool invokeMethod(CState* state, CObjObject *obj, CValue func, int args, int nresults, int offset) {
    // first, set the first argument to the object
    StkPtr temp = cosmoV_getTop(state, args);
    *temp = cosmoV_newObj(obj);

    if (IS_CFUNCTION(func)) {
        callCFunction(state, cosmoV_readCFunction(func), args+1, nresults, offset);
    } else if (IS_CLOSURE(func)) {
        call(state, cosmoV_readClosure(func), args+1, nresults, offset); // offset = 1 so our stack is properly reset
    } else {
        cosmoV_error(state, "Cannot invoke non-function type %s!", cosmoV_typeStr(func));
    }

    return true;
}

// args = # of pass parameters
COSMOVMRESULT cosmoV_call(CState *state, int args, int nresults) {
    StkPtr val = cosmoV_getTop(state, args); // function will always be right above the args

    if (GET_TYPE(*val) != COSMO_TOBJ) {
        cosmoV_error(state, "Cannot call non-function type %s!", cosmoV_typeStr(*val));
        return COSMOVM_RUNTIME_ERR;
    }

    switch (cosmoV_readObj(*val)->type) {
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)cosmoV_readObj(*val);
            if (!call(state, closure, args, nresults, 0)) {
                return COSMOVM_RUNTIME_ERR;
            }
            break;
        }
        case COBJ_METHOD: {
            CObjMethod *method = (CObjMethod*)cosmoV_readObj(*val);
            invokeMethod(state, method->obj, method->func, args, nresults, 1);
            break;
        }
        case COBJ_OBJECT: { // object is being instantiated, making another object
            CObjObject *protoObj = (CObjObject*)cosmoV_readObj(*val);
            CObjObject *newObj = cosmoO_newObject(state);
            newObj->proto = protoObj;
            CValue ret;

            // check if they defined an initializer
            if (cosmoO_getIString(state, protoObj, ISTRING_INIT, &ret)) {
                invokeMethod(state, newObj, ret, args, nresults, 1);
            } else {
                // no default initializer
                cosmoV_error(state, "Expected __init() in proto, object cannot be instantiated!");
                return 0;
            }

            cosmoV_pop(state); // pops the return value, it's unused
            cosmoV_pushValue(state, cosmoV_newObj(newObj));
            break;
        }
        case COBJ_CFUNCTION: {
            // it's a C function, so call it
            CosmoCFunction cfunc = ((CObjCFunction*)cosmoV_readObj(*val))->cfunc;
            callCFunction(state, cfunc, args, nresults, 0);
            break;
        }
        default: 
            cosmoV_error(state, "Cannot call non-function value %s!", cosmoV_typeStr(*val));
            return COSMOVM_RUNTIME_ERR;
    }

    return state->panic ? COSMOVM_RUNTIME_ERR : COSMOVM_OK;
}

static inline bool isFalsey(StkPtr val) {
    return IS_NIL(*val) || (IS_BOOLEAN(*val) && !cosmoV_readBoolean(*val));
}

COSMO_API void cosmoV_makeObject(CState *state, int pairs) {
    StkPtr key, val;
    CObjObject *newObj = cosmoO_newObject(state);
    cosmoV_pushValue(state, cosmoV_newObj(newObj)); // so our GC doesn't free our new object

    for (int i = 0; i < pairs; i++) {
        val = cosmoV_getTop(state, (i*2) + 1);
        key = cosmoV_getTop(state, (i*2) + 2);

        // set key/value pair
        CValue *newVal = cosmoT_insert(state, &newObj->tbl, *key);
        *newVal = *val;
    }

    // once done, pop everything off the stack + push new object
    cosmoV_setTop(state, (pairs * 2) + 1); // + 1 for our object
    cosmoV_pushValue(state, cosmoV_newObj(newObj));
}

COSMO_API void cosmoV_makeDictionary(CState *state, int pairs) {
    StkPtr key, val;
    CObjDict *newObj = cosmoO_newDictionary(state);
    cosmoV_pushValue(state, cosmoV_newObj(newObj)); // so our GC doesn't free our new dictionary

    for (int i = 0; i < pairs; i++) {
        val = cosmoV_getTop(state, (i*2) + 1);
        key = cosmoV_getTop(state, (i*2) + 2);

        // set key/value pair
        CValue *newVal = cosmoT_insert(state, &newObj->tbl, *key);
        *newVal = *val;
    }

    // once done, pop everything off the stack + push new dictionary
    cosmoV_setTop(state, (pairs * 2) + 1); // + 1 for our dictionary
    cosmoV_pushValue(state, cosmoV_newObj(newObj));
}

COSMO_API bool cosmoV_getObject(CState *state, CObjObject *object, CValue key, CValue *val) {
    cosmoV_pushValue(state, cosmoV_newObj(object));
    if (cosmoO_getRawObject(state, object, key, val)) {
        if (IS_OBJ(*val)) {
            if (cosmoV_readObj(*val)->type == COBJ_CLOSURE) { // is it a function? if so, make it a method to the current object
                CObjMethod *method = cosmoO_newMethod(state, (CObjClosure*)cosmoV_readObj(*val), object);
                *val = cosmoV_newObj(method);
            } else if (cosmoV_readObj(*val)->type == COBJ_CFUNCTION) {
                CObjMethod *method = cosmoO_newCMethod(state, (CObjCFunction*)cosmoV_readObj(*val), object);
                *val = cosmoV_newObj(method);
            } // else, just pass the raw object
        }

        cosmoV_pop(state);
        return true;
    }

    cosmoV_pop(state);
    return false;
}

int _dict__next(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 parameter, %d received!", nargs);
        return 0;
    }

    if (!IS_OBJECT(args[0])) {
        cosmoV_error(state, "Expected iterable object, %s received!", cosmoV_typeStr(args[0]));
        return 0;
    }

    CObjObject *obj = cosmoV_readObject(args[0]);
    int index = cosmoO_getUserI(state, obj); // we store the index in the userdata
    CValue val;
    
    cosmoO_getIString(state, obj, ISTRING_RESERVED, &val);

    if (!IS_DICT(val)) {
        return 0; // someone set the __reserved member to something else. this will exit the iterator loop 
    }

    CObjDict *dict = (CObjDict*)cosmoV_readObj(val);

    // while the entry is invalid, go to the next entry
    CTableEntry *entry;
    do {
        entry = &dict->tbl.table[index++];
    } while (IS_NIL(entry->key) && index < dict->tbl.capacity);
    cosmoO_setUserI(state, obj, index); // update the userdata

    if (!IS_NIL(entry->key)) { // if the entry is valid, return it's key and value pair
        cosmoV_pushValue(state, entry->key);
        cosmoV_pushValue(state, entry->val);
        return 2; // we pushed 2 values onto the stack for the return values
    } else {
        return 0; // we have nothing to return, this should exit the iterator loop
    }
}

#define NUMBEROP(typeConst, op)  \
    StkPtr valA = cosmoV_getTop(state, 1); \
    StkPtr valB = cosmoV_getTop(state, 0); \
    if (IS_NUMBER(*valA) && IS_NUMBER(*valB)) { \
        cosmoV_setTop(state, 2); /* pop the 2 values */ \
        cosmoV_pushValue(state, typeConst(cosmoV_readNumber(*valA) op cosmoV_readNumber(*valB))); \
    } else { \
        cosmoV_error(state, "Expected numbers, got %s and %s!", cosmoV_typeStr(*valA), cosmoV_typeStr(*valB)); \
        return -1; \
    } \

// returns -1 if panic
int cosmoV_execute(CState *state) {
    CCallFrame* frame = &state->callFrame[state->frameCount - 1]; // grabs the current frame
    CValue *constants = frame->closure->function->chunk.constants.values; // cache the pointer :)

#define READBYTE() *frame->pc++
#define READUINT() (frame->pc += 2, *(uint16_t*)(&frame->pc[-2]))

    while (!state->panic) {
        /*disasmInstr(&frame->closure->function->chunk, frame->pc - frame->closure->function->chunk.buf, 0);
        printf("\n");*/
        switch (READBYTE()) {
            case OP_LOADCONST: { // push const[uint] to stack
                uint16_t indx = READUINT();
                cosmoV_pushValue(state, constants[indx]);
                break;
            }
            case OP_SETGLOBAL: {
                uint16_t indx = READUINT();
                CValue ident = constants[indx]; // grabs identifier
                CValue *val = cosmoT_insert(state, &state->globals, ident);
                *val = *cosmoV_pop(state); // sets the value in the hash table
                break;
            }
            case OP_GETGLOBAL: {
                uint16_t indx = READUINT();
                CValue ident = constants[indx]; // grabs identifier
                CValue val; // to hold our value
                cosmoT_get(&state->globals, ident, &val);
                cosmoV_pushValue(state, val); // pushes the value to the stack
                break;
            }
            case OP_SETLOCAL: {
                uint8_t indx = READBYTE();
                frame->base[indx] = *cosmoV_pop(state);
                break;
            }
            case OP_GETLOCAL: {
                uint8_t indx = READBYTE();
                cosmoV_pushValue(state, frame->base[indx]);
                break;
            }
            case OP_GETUPVAL: {
                uint8_t indx = READBYTE();
                cosmoV_pushValue(state, *frame->closure->upvalues[indx]->val);
                break;
            }
            case OP_SETUPVAL: {
                uint8_t indx = READBYTE();
                *frame->closure->upvalues[indx]->val = *cosmoV_pop(state);
                break;
            }
            case OP_PEJMP: { // pop equality jump
                uint16_t offset = READUINT();

                if (isFalsey(cosmoV_pop(state))) { // pop, if the condition is false, jump!
                    frame->pc += offset;
                }
                break;
            }
            case OP_EJMP: { // equality jump
                uint16_t offset = READUINT();

                if (isFalsey(cosmoV_getTop(state, 0))) { // if the condition is false, jump!
                    frame->pc += offset;
                }
                break;
            }
            case OP_JMP: { // jump
                uint16_t offset = READUINT();
                frame->pc += offset;
                break;
            }
            case OP_JMPBACK: {
                uint16_t offset = READUINT();
                frame->pc -= offset;
                break;
            }
            case OP_POP: { // pops value off the stack
                cosmoV_setTop(state, READBYTE());
                break;
            }
            case OP_CALL: {
                uint8_t args = READBYTE();
                uint8_t nres = READBYTE();
                COSMOVMRESULT result = cosmoV_call(state, args, nres);
                if (result != COSMOVM_OK) {
                    return result;
                }
                break;
            }
            case OP_CLOSURE: {
                uint16_t index = READUINT();
                CObjFunction *func = cosmoV_readFunction(constants[index]);
                CObjClosure *closure = cosmoO_newClosure(state, func);
                cosmoV_pushValue(state, cosmoV_newObj((CObj*)closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t encoding = READBYTE();
                    uint8_t index = READBYTE();
                    if (encoding == OP_GETUPVAL) {
                        // capture upvalue from current frame's closure
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    } else {
                        // capture local
                        closure->upvalues[i] = captureUpvalue(state, frame->base + index);
                    }
                }
                
                break;
            }
            case OP_CLOSE: {
                closeUpvalues(state, state->top - 1); 
                cosmoV_pop(state);
                break;
            }
            case OP_NEWDICT: {
                uint16_t pairs = READUINT();
                cosmoV_makeDictionary(state, pairs);
                break;
            }
            case OP_INDEX: {
                StkPtr key = cosmoV_getTop(state, 0); // key should be the top of the stack
                StkPtr temp = cosmoV_getTop(state, 1); // after that should be the dictionary

                // sanity check
                if (IS_OBJ(*temp)) {
                    CValue val; // to hold our value

                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: {
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);

                            cosmoT_get(&dict->tbl, *key, &val);
                            break;
                        }
                        case COBJ_OBJECT: { // check for __index
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);

                            if (!cosmoO_indexObject(state, object, *key, &val)) // if returns false, cosmoV_error was called
                                return -1;

                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't index type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }

                    cosmoV_setTop(state, 2); // pops the object & the key
                    cosmoV_pushValue(state, val); // pushes the field result
                } else {
                    cosmoV_error(state, "Couldn't index type %s!", cosmoV_typeStr(*temp));
                }

                break;
            }
            case OP_NEWINDEX: {
                StkPtr value = cosmoV_getTop(state, 0); // value is at the top of the stack
                StkPtr key = cosmoV_getTop(state, 1);
                StkPtr temp = cosmoV_getTop(state, 2); // object is after the key

                // sanity check
                if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: { // index and set table
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);
                            CValue *newVal = cosmoT_insert(state, &dict->tbl, *key);

                            *newVal = *value; // set the index
                            break;
                        }
                        case COBJ_OBJECT: { // check for __newindex
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);

                            if (!cosmoO_newIndexObject(state, object, *key, *value)) // if it returns false, cosmoV_error was called
                                return -1;

                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't set index with type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }

                    // pop everything off the stack
                    cosmoV_setTop(state, 3);
                } else {
                    cosmoV_error(state, "Couldn't set index with type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                break;
            }
            case OP_NEWOBJECT: {
                uint16_t pairs = READUINT();
                cosmoV_makeObject(state, pairs);
                break;
            }
            case OP_GETOBJECT: {
                CValue val; // to hold our value
                StkPtr key = cosmoV_getTop(state, 0); // key should be the top of the stack
                StkPtr temp = cosmoV_getTop(state, 1); // after that should be the object

                // sanity check
                if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: { // syntax sugar, makes "namespaces" possible
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);

                            cosmoT_get(&dict->tbl, *key, &val);
                            break;
                        }
                        case COBJ_OBJECT: {
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);

                            cosmoV_getObject(state, object, *key, &val);
                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't get from type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't get from type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_setTop(state, 2); // pops the object & the key
                cosmoV_pushValue(state, val); // pushes the field result
                break;
            }
            case OP_SETOBJECT: {
                StkPtr value = cosmoV_getTop(state, 0); // value is at the top of the stack
                StkPtr key = cosmoV_getTop(state, 1);
                StkPtr temp = cosmoV_getTop(state, 2); // object is after the key

                // sanity check
                if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: { // index and set the table
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);
                            CValue *newVal = cosmoT_insert(state, &dict->tbl, *key);

                            *newVal = *value; // set the index
                            break;
                        }
                        case COBJ_OBJECT: {
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);

                            cosmoO_setRawObject(state, object, *key, *value);
                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't set a field on type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't set a field on type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                // pop everything off the stack
                cosmoV_setTop(state, 3);
                break;
            }
            case OP_INVOKE: { // this is an optimization made by the parser, instead of allocating a CObjMethod every time we want to invoke a method, we shrink it down into one minimal instruction!
                CValue val; // to hold our value
                uint8_t args = READBYTE();
                uint8_t nres = READBYTE();
                StkPtr key = cosmoV_getTop(state, args); // grabs key from stack
                StkPtr temp = cosmoV_getTop(state, args+1); // grabs object from stack

                // sanity check
                if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: { // again, syntax sugar ("""""namespaces""""")
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);

                            cosmoT_get(&dict->tbl, *key, &val);

                            // call closure/cfunction
                            if (IS_CFUNCTION(val)) {
                                callCFunction(state, cosmoV_readCFunction(val), args, nres, -1);
                            } else if (IS_CLOSURE(val)) {
                                call(state, cosmoV_readClosure(val), args, nres, -1);
                            } else {
                                cosmoV_error(state, "Cannot call non-function value %s!", cosmoV_typeStr(val));
                                return -1;
                            }
                            break;
                        }
                        case COBJ_OBJECT: {
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);

                            cosmoO_getRawObject(state, object, *key, &val); // we use cosmoO_getRawObject instead of the cosmoV_getObject wrapper so we get the raw value from the object instead of the CObjMethod wrapper

                            // now invoke the method!
                            invokeMethod(state, object, val, args, nres, 0);
                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't get from type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't get from type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                break;
            }
            case OP_ITER: {
                StkPtr temp = cosmoV_getTop(state, 0); // should be the object/dictionary

                if (!IS_OBJ(*temp)) {
                    cosmoV_error(state, "Couldn't iterate over non-iterator type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                switch (cosmoV_readObj(*temp)->type) {
                    case COBJ_DICT: {
                        CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);

                        cosmoV_pushValue(state, cosmoV_newObj(state->iStrings[ISTRING_RESERVED])); // key
                        cosmoV_pushValue(state, cosmoV_newObj(dict)); // value

                        cosmoV_pushString(state, "__next"); // key
                        CObjCFunction *dict_next = cosmoO_newCFunction(state, _dict__next);
                        cosmoV_pushValue(state, cosmoV_newObj(dict_next)); // value

                        cosmoV_makeObject(state, 2); // pushes the new object to the stack

                        CObjObject *obj = cosmoV_readObject(*(cosmoV_getTop(state, 0)));
                        cosmoO_setUserI(state, obj, 0); // increment for iterator

                        // make our CObjMethod for OP_NEXT to call
                        CObjMethod *method = cosmoO_newCMethod(state, dict_next, obj);

                        cosmoV_setTop(state, 2); // pops the object & the dict
                        cosmoV_pushValue(state, cosmoV_newObj(method)); // pushes the method for OP_NEXT
                        break;
                    }
                    case COBJ_OBJECT: {
                        CObjObject *obj = cosmoV_readObject(*temp);
                        CValue val;

                        // grab __iter & call it
                        if (cosmoO_getIString(state, obj, ISTRING_ITER, &val)) {
                            cosmoV_pop(state); // pop the object from the stack
                            cosmoV_pushValue(state, val);
                            cosmoV_pushValue(state, cosmoV_newObj(obj));
                            cosmoV_call(state, 1, 1); // we expect 1 return value on the stack, the iterable object

                            StkPtr iObj = cosmoV_getTop(state, 0);

                            if (!IS_OBJECT(*iObj)) {
                                cosmoV_error(state, "Expected iterable object! '__iter' returned %s, expected object!", cosmoV_typeStr(*iObj));
                                return -1;
                            }

                            CObjObject *obj = (CObjObject*)cosmoV_readObj(*iObj);

                            cosmoV_getObject(state, obj, cosmoV_newObj(state->iStrings[ISTRING_NEXT]), iObj);
                        } else {
                            cosmoV_error(state, "Expected iterable object! '__iter' not defined!");
                            return -1;
                        }

                        break;
                    }
                    default:
                        cosmoV_error(state, "Couldn't iterate over non-iterator type %s!", cosmoV_typeStr(*temp));
                        return -1;
                }
                break;
            }
            case OP_NEXT: {
                uint8_t nresults = READBYTE();
                uint16_t jump = READUINT();
                StkPtr temp = cosmoV_getTop(state, 0); // we don't actually pop this off the stack

                if (!IS_METHOD(*temp)) {
                    cosmoV_error(state, "Expected '__next' to be a method, got type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                cosmoV_pushValue(state, *temp);
                cosmoV_call(state, 0, nresults);

                if (IS_NIL(*(cosmoV_getTop(state, 0)))) { // __next returned a nil, which means to exit the loop
                    cosmoV_setTop(state, nresults); // pop the return values
                    frame->pc += jump;
                }
                break;
            }
            case OP_ADD: { // pop 2 values off the stack & try to add them together
                NUMBEROP(cosmoV_newNumber, +);
                break;
            }
            case OP_SUB: { // pop 2 values off the stack & try to subtracts them
                NUMBEROP(cosmoV_newNumber, -)
                break;
            }
            case OP_MULT: { // pop 2 values off the stack & try to multiplies them together
                NUMBEROP(cosmoV_newNumber, *)
                break;
            }
            case OP_DIV: { // pop 2 values off the stack & try to divides them
                NUMBEROP(cosmoV_newNumber, /)
                break;
            }
            case OP_MOD: {
                StkPtr valA = cosmoV_getTop(state, 1);
                StkPtr valB = cosmoV_getTop(state, 0);
                if (IS_NUMBER(*valA) && IS_NUMBER(*valB)) {
                    cosmoV_setTop(state, 2); /* pop the 2 values */
                    cosmoV_pushValue(state, cosmoV_newNumber(fmod(cosmoV_readNumber(*valA), cosmoV_readNumber(*valB))));
                } else { \
                    cosmoV_error(state, "Expected numbers, got %s and %s!", cosmoV_typeStr(*valA), cosmoV_typeStr(*valB));
                    return -1; \
                } \
                break;
            }
            case OP_NOT: {
                cosmoV_pushBoolean(state, isFalsey(cosmoV_pop(state)));
                break;
            }
            case OP_NEGATE: { // pop 1 value off the stack & try to negate
                StkPtr val = cosmoV_getTop(state, 0);

                if (IS_NUMBER(*val)) {
                    cosmoV_pop(state);
                    cosmoV_pushNumber(state, -(cosmoV_readNumber(*val)));
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }
                break;
            }
            case OP_COUNT: { // pop 1 value off the stack & if it's a dictionary return the amount of active entries it has
                StkPtr temp = cosmoV_getTop(state, 0);

                if (!IS_OBJ(*temp) || cosmoV_readObj(*temp)->type != COBJ_DICT) {
                    cosmoV_error(state, "Expected object, got %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);
                cosmoV_pop(state);
                cosmoV_pushNumber(state, cosmoT_count(&dict->tbl)); // pushes the count onto the stack
                break;
            }
            case OP_CONCAT: {
                uint8_t vals = READBYTE();
                cosmoV_concat(state, vals);
                break;
            }
            case OP_INCLOCAL: { // this leaves the value on the stack
                int8_t inc = READBYTE() - 128; // amount we're incrementing by
                uint8_t indx = READBYTE();
                StkPtr val = &frame->base[indx];

                // check that it's a number value
                if (IS_NUMBER(*val)) { 
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }

                break;
            }
            case OP_INCGLOBAL: {
                int8_t inc = READBYTE() - 128; // amount we're incrementing by
                uint16_t indx = READUINT();
                CValue ident = constants[indx]; // grabs identifier
                CValue *val = cosmoT_insert(state, &state->globals, ident);

                // check that it's a number value
               if (IS_NUMBER(*val)) { 
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }

                break;
            }
            case OP_INCUPVAL: {
                int8_t inc = READBYTE() - 128; // amount we're incrementing by
                uint8_t indx = READBYTE();
                CValue *val = frame->closure->upvalues[indx]->val;

                // check that it's a number value
                if (IS_NUMBER(*val)) { 
                    cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                    *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                } else {
                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                    return -1;
                }

                break;
            }
            case OP_INCINDEX: {
                int8_t inc = READBYTE() - 128; // amount we're incrementing by
                StkPtr temp = cosmoV_getTop(state, 1); // object should be above the key
                StkPtr key = cosmoV_getTop(state, 0); // grabs key

                 if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_DICT: {
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);
                            CValue *val = cosmoT_insert(state, &dict->tbl, *key);

                            // pops dict & key from stack
                            cosmoV_setTop(state, 2);

                            if (IS_NUMBER(*val)) { 
                                cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                                *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                            } else {
                                cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                                return -1;
                            }
                            break;
                        }
                        case COBJ_OBJECT: {
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);
                            CValue val;
                            
                            // call __index
                            if (cosmoO_indexObject(state, object, *key, &val)) {
                                if (IS_NUMBER(val)) { 
                                    cosmoV_pushValue(state, val); // pushes old value onto the stack :)

                                    // call __newindex
                                    cosmoO_newIndexObject(state, object, *key, cosmoV_newNumber(cosmoV_readNumber(val) + inc));
                                } else {
                                    cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(val));
                                    return -1;
                                }
                            }
                            break;
                        }
                        default:
                            cosmoV_error(state, "Couldn't set index with type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't set index with type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                break;
            }
            case OP_INCOBJECT: {
                int8_t inc = READBYTE() - 128; // amount we're incrementing by
                uint16_t indx = READUINT();
                StkPtr temp = cosmoV_getTop(state, 0); // object should be at the top of the stack
                CValue ident = constants[indx]; // grabs identifier

                // sanity check
                if (IS_OBJ(*temp)) {
                    switch (cosmoV_readObj(*temp)->type) {
                        case COBJ_OBJECT: {
                            CObjObject *object = (CObjObject*)cosmoV_readObj(*temp);
                            CValue val;
                            
                            cosmoO_getRawObject(state, object, ident, &val);

                            // pop the object off the stack
                            cosmoV_pop(state);

                            // check that it's a number value
                            if (IS_NUMBER(val)) { 
                                cosmoV_pushValue(state, val); // pushes old value onto the stack :)
                                cosmoO_setRawObject(state, object, ident, cosmoV_newNumber(cosmoV_readNumber(val) + inc));
                            } else {
                                cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(val));
                                return -1;
                            }
                            break;
                        }
                        case COBJ_DICT: {
                            CObjDict *dict = (CObjDict*)cosmoV_readObj(*temp);
                            CValue *val = cosmoT_insert(state, &dict->tbl, ident);

                            // pops dict from stack
                            cosmoV_pop(state);

                            if (IS_NUMBER(*val)) { 
                                cosmoV_pushValue(state, *val); // pushes old value onto the stack :)
                                *val = cosmoV_newNumber(cosmoV_readNumber(*val) + inc);
                            } else {
                                cosmoV_error(state, "Expected number, got %s!", cosmoV_typeStr(*val));
                                return -1;
                            }
                            break;
                        }
                        default: 
                            cosmoV_error(state, "Couldn't set a field on type %s!", cosmoV_typeStr(*temp));
                            return -1;
                    }
                } else {
                    cosmoV_error(state, "Couldn't set a field on type %s!", cosmoV_typeStr(*temp));
                    return -1;
                }

                break;
            }
            case OP_EQUAL: {
                // pop vals
                StkPtr valB = cosmoV_pop(state);
                StkPtr valA = cosmoV_pop(state);

                // compare & push
                cosmoV_pushBoolean(state, cosmoV_equal(*valA, *valB));
                break;
            }
            case OP_GREATER: {
                NUMBEROP(cosmoV_newBoolean, >)
                break;
            }
            case OP_LESS: {
                NUMBEROP(cosmoV_newBoolean, <)
                break;
            }
            case OP_GREATER_EQUAL: {
                NUMBEROP(cosmoV_newBoolean, >=)
                break;
            }
            case OP_LESS_EQUAL: {
                NUMBEROP(cosmoV_newBoolean, <=)
                break;
            }
            case OP_TRUE:   cosmoV_pushBoolean(state, true); break;
            case OP_FALSE:  cosmoV_pushBoolean(state, false); break;
            case OP_NIL:    cosmoV_pushValue(state, cosmoV_newNil()); break;
            case OP_RETURN: {
                uint8_t res = READBYTE();
                return res;
            }
            default:
                CERROR("unknown opcode!");
                exit(0);
        }
        //cosmoV_printStack(state);
    }

#undef READBYTE
#undef READUINT

    // we'll only reach this is state->panic is true
    return -1;
}

#undef NUMBEROP