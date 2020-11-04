#include "cvm.h"
#include "cstate.h"
#include "cdebug.h"
#include "cmem.h"

#include <stdarg.h>
#include <string.h>

void runtimeError(CState *state, const char *format, ...) {
    if (state->panic)
        return;

    // print stack trace
    for (int i = 0; i < state->frameCount; i++) {
        CCallFrame *frame = &state->callFrame[i];
        CObjFunction *function = frame->closure->function;
        CChunk *chunk = &function->chunk;

        int line = chunk->lineInfo[frame->pc - chunk->buf - 1];

        if (i == state->frameCount - 1) { // it's the last call frame, prepare for the objection to be printed
            fprintf(stderr, "Objection on [line %d] in ", line);
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
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    // TODO: push error onto the stack :P
    state->panic = true;
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
    CCallFrame *frame = &state->callFrame[state->frameCount++];
    frame->base = state->top - args - 1; // - 1 for the function
    frame->pc = closure->function->chunk.buf;
    frame->closure = closure;
}

void popCallFrame(CState *state) {
    closeUpvalues(state, state->callFrame[state->frameCount - 1].base); // close any upvalue still open

    state->top = state->callFrame[state->frameCount - 1].base; // resets the stack
    state->frameCount--;
}

CObjString *cosmoV_concat(CState *state, CObjString *strA, CObjString *strB) {
    size_t sz = strA->length + strB->length;
    char *buf = cosmoM_xmalloc(state, sz + 1); // +1 for null terminator
    
    memcpy(buf, strA->str, strA->length);
    memcpy(buf + strA->length, strB->str, strB->length);
    buf[sz] = '\0';

    return cosmoO_takeString(state, buf, sz);
}

int cosmoV_execute(CState *state);

typedef enum {
    CALL_CLOSURE,
    CALL_CFUNCTION
} preCallResult;

int cosmoV_preCall(CState *state, int args, int nresults) {
    return -1;
}

// args = # of pass parameters, nresults = # of expected results
COSMOVMRESULT cosmoV_call(CState *state, int args, int nresults) {
    StkPtr val = cosmoV_getTop(state, args); // function will always be right above the args

    if (!(val->type == COSMO_TOBJ)) {
        runtimeError(state, "Cannot call non-function value!");
        return COSMOVM_RUNTIME_ERR;
    }

    switch (val->val.obj->type) {
        case COBJ_CLOSURE: {
            CObjClosure *closure = (CObjClosure*)(val->val.obj);

            // missmatched args, thats an obvious user error, so error.
            if (args != closure->function->args) {
                runtimeError(state, "Expected %d parameters for %s, got %d!", closure->function->args, closure->function->name == NULL ? UNNAMEDCHUNK : closure->function->name->str, args);
                return COSMOVM_RUNTIME_ERR;
            }
            
            // load function into callframe
            pushCallFrame(state, closure, closure->function->args);

            // execute
            int res = cosmoV_execute(state);

            // so, since we can have any # of results, we need to move the expected results to the original call frame (that means popping/adding however many results)
            CValue* results = state->top;

            // pop the callframe and return result :)
            popCallFrame(state);

            // return the results to the stack
            for (int i = 1; i <= nresults; i++) {
                if (i <= res)
                    cosmoV_pushValue(state, results[-i]);
                else
                    cosmoV_pushValue(state, cosmoV_newNil());
            }

            break;
        }
        case COBJ_CFUNCTION: {
            // it's a C function, so call it
            CosmoCFunction cfunc = ((CObjCFunction*)(val->val.obj))->cfunc;
            CValue *savedBase = state->top - args - 1;

            cosmoM_freezeGC(state); // we don't want a GC event during c api because we don't actually trust the user to know how to evade the GC
            int res = cfunc(state, args, state->top - args);
            cosmoM_unfreezeGC(state);

            // so, since we can have any # of results, we need to move the expected results to the original call frame
            CValue* results = state->top;
            state->top = savedBase;

            // return the results to the stack
            for (int i = 1; i <= nresults; i++) {
                if (i <= res)
                    cosmoV_pushValue(state, results[-i]);
                else
                    cosmoV_pushValue(state, cosmoV_newNil());
            }
            
            break;
        }
        default: 
            runtimeError(state, "Cannot call non-function value!");
            return COSMOVM_RUNTIME_ERR;
    }

    return state->panic ? COSMOVM_RUNTIME_ERR : COSMOVM_OK;
}

static inline bool isFalsey(StkPtr val) {
    return val->type == COSMO_TNIL || (val->type == COSMO_TBOOLEAN && !val->val.b);
}

#define BINARYOP(typeConst, op)  \
    StkPtr valA = cosmoV_getTop(state, 1); \
    StkPtr valB = cosmoV_getTop(state, 0); \
    if (valA->type == COSMO_TNUMBER && valB->type == COSMO_TNUMBER) { \
        cosmoV_setTop(state, 2); /* pop the 2 values */ \
        cosmoV_pushValue(state, typeConst((valA->val.num) op (valB->val.num))); \
    } else { \
        runtimeError(state, "Expected number! got %d and %d", valA->type, valB->type); \
    } \


// returns -1 if error, otherwise returns ammount of results
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
                frame->base[READBYTE()] = *cosmoV_pop(state);
                break;
            }
            case OP_GETLOCAL: {
                cosmoV_pushValue(state, frame->base[READBYTE()]);
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
            case OP_PEJMP: {
                uint16_t offset = READUINT();

                if (isFalsey(cosmoV_pop(state))) { // pop, if the condition is false, jump!
                    frame->pc += offset;
                }
                break;
            }
            case OP_EJMP: {
                uint16_t offset = READUINT();

                if (isFalsey(cosmoV_getTop(state, 0))) { // if the condition is false, jump!
                    frame->pc += offset;
                }
                break;
            }
            case OP_JMP: {
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
                uint8_t results = READBYTE();
                COSMOVMRESULT result = cosmoV_call(state, args, results);
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
            case OP_NEWOBJECT: {
                uint8_t entries = READBYTE();
                StkPtr key, val;
                CObjObject *newObj = cosmoO_newObject(state, entries * 3); // start the table with enough space to hopefully prevent reallocation since that's costly
                cosmoV_pushValue(state, cosmoV_newObj(newObj)); // so our GC doesn't free our new object

                for (int i = 0; i < entries; i++) {
                    val = cosmoV_getTop(state, (i*2) + 2);
                    key = cosmoV_getTop(state, (i*2) + 1);

                    // set key/value pair
                    CValue *newVal = cosmoT_insert(state, &newObj->tbl, *key);
                    *newVal = *val;
                }

                // once done, pop everything off the stack + push new object
                cosmoV_setTop(state, (entries * 2) + 1);
                cosmoV_pushValue(state, cosmoV_newObj(newObj));
                break;
            }
            case OP_GETOBJECT: {
                StkPtr key = cosmoV_getTop(state, 0); // key should be the top of the stack
                StkPtr temp = cosmoV_getTop(state, 1); // after that should be the object

                // sanity check
                if (!(temp->type == COSMO_TOBJ) || !(temp->val.obj->type == COBJ_OBJECT)) {
                    runtimeError(state, "Couldn't get from non-object!");
                    break;
                }

                CObjObject *object = (CObjObject*)temp->val.obj;
                CValue val; // to hold our value

                cosmoT_get(&object->tbl, *key, &val);
                cosmoV_setTop(state, 2); // pops the object & the key
                cosmoV_pushValue(state, val); // pushes the field result
                break;
            }
            case OP_SETOBJECT: {
                StkPtr value = cosmoV_getTop(state, 0); // value is at the top of the stack
                StkPtr key = cosmoV_getTop(state, 1);
                StkPtr temp = cosmoV_getTop(state, 2); // object is after the key

                // sanity check
                if (!(temp->type == COSMO_TOBJ) || !(temp->val.obj->type == COBJ_OBJECT)) {
                    runtimeError(state, "Couldn't set a field on a non-object!");
                    break;
                }

                CObjObject *object = (CObjObject*)temp->val.obj;
                CValue *newVal = cosmoT_insert(state, &object->tbl, *key);
                *newVal = *value;

                // pop everything off the stack
                cosmoV_setTop(state, 3);
                break;
            }
            case OP_ADD: { // pop 2 values off the stack & try to add them together
                BINARYOP(cosmoV_newNumber, +);
                break;
            }
            case OP_SUB: { // pop 2 values off the stack & try to subtracts them
                BINARYOP(cosmoV_newNumber, -)
                break;
            }
            case OP_MULT: { // pop 2 values off the stack & try to multiplies them together
                BINARYOP(cosmoV_newNumber, *)
                break;
            }
            case OP_DIV: { // pop 2 values off the stack & try to divides them
                BINARYOP(cosmoV_newNumber, /)
                break;
            }
            case OP_NOT: {
                cosmoV_pushValue(state, cosmoV_newBoolean(isFalsey(cosmoV_pop(state))));
                break;
            }
            case OP_NEGATE: { // pop 1 value off the stack & try to negate
                StkPtr val = cosmoV_getTop(state, 0);

                if (val->type == COSMO_TNUMBER) {
                    cosmoV_pop(state);
                    cosmoV_pushValue(state, cosmoV_newNumber(-(val->val.num)));
                } else {
                    runtimeError(state, "Expected number!");
                }
                break;
            }
            case OP_CONCAT: {
                uint8_t vals = READBYTE();
                StkPtr start = state->top - vals;
                StkPtr end = cosmoV_getTop(state, 0);
                StkPtr current;

                CObjString *result = cosmoV_toString(state, *start);
                for (StkPtr current = start + 1; current <= end; current++) {
                    cosmoV_pushValue(state, cosmoV_newObj(result)); // so our GC can find our current result string
                    CObjString *otherStr = cosmoV_toString(state, *current);
                    cosmoV_pushValue(state, cosmoV_newObj(otherStr)); // also so our GC won't free otherStr
                    result = cosmoV_concat(state, result, otherStr);

                    cosmoV_setTop(state, 2); // pop result & otherStr off the stack
                }

                state->top = start;
                cosmoV_pushValue(state, cosmoV_newObj(result));
                break;
            }
            case OP_EQUAL: {
                // pop vals
                StkPtr valB = cosmoV_pop(state);
                StkPtr valA = cosmoV_pop(state);

                // compare & push
                cosmoV_pushValue(state, cosmoV_newBoolean(cosmoV_equal(*valA, *valB)));
                break;
            }
            case OP_GREATER: {
                BINARYOP(cosmoV_newBoolean, >)
                break;
            }
            case OP_LESS: {
                BINARYOP(cosmoV_newBoolean, <)
                break;
            }
            case OP_GREATER_EQUAL: {
                BINARYOP(cosmoV_newBoolean, >=)
                break;
            }
            case OP_LESS_EQUAL: {
                BINARYOP(cosmoV_newBoolean, <=)
                break;
            }
            case OP_TRUE:   cosmoV_pushValue(state, cosmoV_newBoolean(true)); break;
            case OP_FALSE:  cosmoV_pushValue(state, cosmoV_newBoolean(false)); break;
            case OP_NIL:    cosmoV_pushValue(state, cosmoV_newNil()); break;
            case OP_RETURN: {
                uint8_t results = READBYTE();
                return results;
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
    return COSMOVM_RUNTIME_ERR;
}

#undef BINARYOP