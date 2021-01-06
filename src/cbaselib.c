#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

int cosmoB_print(CState *state, int nargs, CValue *args) {
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }
    printf("\n");

    return 0; // print doesn't return any args
}

int cosmoB_assert(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "assert() expected 1 argument, got %d!", nargs);
        return 0; // nothing pushed onto the stack to return
    }

    if (!IS_BOOLEAN(args[0])) {
        cosmoV_typeError(state, "assert()", "<boolean>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    if (!cosmoV_readBoolean(args[0])) { // expression passed was false, error!
        cosmoV_error(state, "assert() failed!");
    } // else do nothing :)

    return 0;
}

int cosmoB_type(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "type() expected 1 argument, got %d!", nargs);
        return 0;
    }

    // push the type string to the stack
    cosmoV_pushString(state, cosmoV_typeStr(args[0]));
    return 1; // 1 return value, the type string :D
}

int cosmoB_pcall(CState *state, int nargs, CValue *args) {
    if (nargs < 1) {
        cosmoV_error(state, "pcall() expected at least 1 argument!");
        return 0;
    }

    // call the passed callable
    COSMOVMRESULT res = cosmoV_pcall(state, nargs-1, 1);

    // insert false before the result
    cosmo_insert(state, 0, cosmoV_newBoolean(res == COSMOVM_OK));
    return 2;
}

// ================================================================ [STRING.*] ================================================================

// string.sub
int cosmoB_sSub(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
            cosmoV_typeError(state, "string.sub()", "<string>, <number>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
            return 0;
        }

        CObjString *str = cosmoV_readString(args[0]);
        cosmo_Number indx = cosmoV_readNumber(args[1]);

        // make sure we stay within memory
        if (indx < 0 || indx >= str->length) {
            cosmoV_error(state, "string.sub() expected index to be 0-%d, got %d!", str->length, indx);
            return 0;
        }

        cosmoV_pushLString(state, str->str + ((int)indx), str->length - ((int)indx));
    } else if (nargs == 3) {
        if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
            cosmoV_typeError(state, "string.sub()", "<string>, <number>, <number>", "%s, %s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]), cosmoV_typeStr(args[2]));
            return 0;
        }

        CObjString *str = cosmoV_readString(args[0]);
        cosmo_Number indx = cosmoV_readNumber(args[1]);
        cosmo_Number length = cosmoV_readNumber(args[2]);

        // make sure we stay within memory
        if (indx + length < 0 || indx + length >= str->length || indx < 0 || indx >= str->length) {
            cosmoV_error(state, "string.sub() expected subbed string goes out of bounds, max length is %d!", str->length);
            return 0;
        }

        cosmoV_pushLString(state, str->str + ((int)indx), ((int)length));
    } else {
        cosmoV_error(state, "Expected 2 or 3 arguments, got %d!", nargs);
        return 0;
    }

    return 1;
}

void cosmoB_loadLibrary(CState *state) {
    // print
    cosmoV_pushString(state, "print");
    cosmoV_pushCFunction(state, cosmoB_print);

    // assert (for unit testing)
    cosmoV_pushString(state, "assert");
    cosmoV_pushCFunction(state, cosmoB_assert);

    // type
    cosmoV_pushString(state, "type");
    cosmoV_pushCFunction(state, cosmoB_type);

    // pcall
    cosmoV_pushString(state, "pcall");
    cosmoV_pushCFunction(state, cosmoB_pcall);

    // string.
    cosmoV_pushString(state, "string");

    // sub
    cosmoV_pushString(state, "sub");
    cosmoV_pushCFunction(state, cosmoB_sSub);
    
    cosmoV_makeDictionary(state, 1);
    // string.

    // register these all to the global table
    cosmoV_register(state, 5);
}

// ================================================================ [DEBUG] ================================================================

int cosmoB_dsetProto(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        CObjObject *obj = cosmoV_readObject(args[0]); // object to set proto too
        CObjObject *proto = cosmoV_readObject(args[1]);

        obj->proto = proto; // boom done
    } else {
        cosmoV_error(state, "Expected 2 parameters, got %d!", nargs);
    }

    return 0; // nothing
}

int cosmoB_dgetProto(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 argument, got %d!", nargs);
    }

    cosmoV_pushValue(state, cosmoV_newObj(cosmoV_readObject(args[0])->proto)); // just return the proto

    return 1; // 1 result
}

void cosmoB_loadDebug(CState *state) {
    // make __getter object for debug proto
    cosmoV_pushString(state, "__getter");

    // key & value pair
    cosmoV_pushString(state, "__proto"); // key
    cosmoV_pushCFunction(state, cosmoB_dgetProto); // value

    cosmoV_makeDictionary(state, 1);

    // make __setter object
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "__proto");
    cosmoV_pushCFunction(state, cosmoB_dsetProto);

    cosmoV_makeDictionary(state, 1);

    // we call makeObject leting it know there are 2 sets of key & value pairs on the stack
    cosmoV_makeObject(state, 2);

    // set debug proto to the debug object
    state->protoObj = cosmoV_readObject(*cosmoV_pop(state));
}
