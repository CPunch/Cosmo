#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

#include <math.h>

// ================================================================ [BASELIB] ================================================================

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

    // unfreeze the state GC before calling the function
    cosmoM_unfreezeGC(state);

    // call the passed callable
    COSMOVMRESULT res = cosmoV_pcall(state, nargs-1, 1);

    // insert false before the result
    cosmo_insert(state, 0, cosmoV_newBoolean(res == COSMOVM_OK));

    // refreeze the state GC so it can be properly unfrozen
    cosmoM_freezeGC(state);
    return 2;
}

int cosmoB_tonumber(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "tonumber() expected 1 argument, got %d!", nargs);
        return 0;
    }

    cosmoV_pushNumber(state, cosmoV_toNumber(state, args[0]));
    return 1;
}

int cosmoB_tostring(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "tostring() expected 1 argument, got %d!", nargs);
        return 0;
    }

    cosmoV_pushObj(state, (CObj*)cosmoV_toString(state, args[0]));
    return 1;
}

int cosmoB_loadstring(CState *state, int nargs, CValue *args) {
    if (nargs < 1) {
        cosmoV_error(state, "loadstring() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "loadstring()", "<string>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    CObjString *str = cosmoV_readString(args[0]);
    bool res = cosmoV_compileString(state, str->str, "");

    cosmo_insert(state, 0, cosmoV_newBoolean(res));
    return 2; // <boolean>, <closure> or <error>
}

void cosmoB_loadLibrary(CState *state) {
    const char *identifiers[] = {
        "print",
        "assert",
        "type",
        "pcall",
        "tonumber",
        "tostring"
        "loadstring"
    };

    CosmoCFunction baseLib[] = {
        cosmoB_print,
        cosmoB_assert,
        cosmoB_type,
        cosmoB_pcall,
        cosmoB_tonumber,
        cosmoB_tostring,
        cosmoB_loadstring
    };

    int i;
    for (i = 0; i < sizeof(identifiers)/sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, baseLib[i]);
    }

    // register all the pushed c functions and the strings as globals
    cosmoV_register(state, i);

    // load other libraries
    cosmoB_loadStrLib(state);
    cosmoB_loadMathLib(state);
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
            cosmoV_error(state, "string.sub() expected index to be 0-%d, got %d!", str->length - 1, indx);
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
        cosmoV_error(state, "string.sub() expected 2 or 3 arguments, got %d!", nargs);
        return 0;
    }

    return 1;
}

// string.find
int cosmoB_sFind(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
            cosmoV_typeError(state, "string.find()", "<string>, <string>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
            return 0;
        }

        CObjString *str = cosmoV_readString(args[0]);
        CObjString *ptrn = cosmoV_readString(args[1]);

        char *indx = strstr(str->str, ptrn->str);

        // failed, we have nothing to return
        if (indx == NULL)
            return 0;

        // success! push the index
        cosmoV_pushNumber(state, indx - str->str);
    } else if (nargs == 3) {
        if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_NUMBER(args[2])) {
            cosmoV_typeError(state, "string.find()", "<string>, <string>, <number>", "%s, %s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]), cosmoV_typeStr(args[2]));
            return 0;
        }

        CObjString *str = cosmoV_readString(args[0]);
        CObjString *ptrn = cosmoV_readString(args[1]);
        int startIndx = (int)cosmoV_readNumber(args[2]);

        char *indx = strstr(str->str + startIndx, ptrn->str);

        // failed, we have nothing to return
        if (indx == NULL)
            return 0;

        // success! push the index
        cosmoV_pushNumber(state, indx - str->str);
    } else {
        cosmoV_error(state, "string.find() expected 2 or 3 arguments, got %d!", nargs);
        return 0;
    }

    return 1;
}

// string.split
int cosmoB_sSplit(CState *state, int nargs, CValue *args) {
    if (nargs != 2) {
        cosmoV_error(state, "string.split() expected 2 arguments, got %d!", nargs);
        return 0;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        cosmoV_typeError(state, "string.split()", "<string>, <string>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        return 0;
    }

    CObjString *str = cosmoV_readString(args[0]);
    CObjString *ptrn = cosmoV_readString(args[1]);

    int nEntries = 0;
    char *indx = str->str;
    char *nIndx;

    // while there are still patterns to match in the string, push the split strings onto the stack
    do {
        nIndx = strstr(indx, ptrn->str);

        cosmoV_pushNumber(state, nEntries++);
        cosmoV_pushLString(state, indx, nIndx == NULL ? str->length - (indx - str->str) : nIndx - indx);

        indx = nIndx + ptrn->length;
    } while (nIndx != NULL);

    // finally, make a table out of the pushed entries
    cosmoV_makeTable(state, nEntries);
    return 1;
}

// string.byte
int cosmoB_sByte(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "string.byte() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "string.byte", "<string>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }
    
    CObjString *str = cosmoV_readString(args[0]);

    if (str->length < 1) {
        // the length of the string is less than 1, in the future I might throw an error for this, but 
        // for now im going to copy lua and just return a nil
        return 0;
    }

    // push the character byte and return
    cosmoV_pushNumber(state, (int)str->str[0]);
    return 1;
}

// string.char
int cosmoB_sChar(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "string.char() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "string.char", "<number>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    // small side effect of truncating the number, but ignoring the decimal instead of throwing an error is the better option imo
    int num = (int)cosmoV_readNumber(args[0]);
    char c = num;

    if (num > 255 || num < 0) {
        cosmoV_error(state, "Character expected to be in range 0-255, got %d!", num);
        return 0;
    }

    // basically, treat the c value on the stack as an """"array"""" with a length of 1
    cosmoV_pushLString(state, &c, 1);
    return 1;
}

void cosmoB_loadStrLib(CState *state) {
    const char *identifiers[] = {
        "sub",
        "find",
        "split",
        "byte",
        "char"
    };

    CosmoCFunction strLib[] = {
        cosmoB_sSub,
        cosmoB_sFind,
        cosmoB_sSplit,
        cosmoB_sByte,
        cosmoB_sChar
    };

    // make string library object
    cosmoV_pushString(state, "string");
    int i;
    for (i = 0; i < sizeof(identifiers)/sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, strLib[i]);
    }

    // make the object and set the protoobject for all strings
    CObjObject *obj = cosmoV_makeObject(state, i);
    cosmoV_registerProtoObject(state, COBJ_STRING, obj);

    // register "string" to the global table
    cosmoV_register(state, 1);
}

// ================================================================ [MATH] ================================================================

// math.abs
int cosmoB_mAbs(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "math.abs() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.abs", "<number>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    cosmoV_pushNumber(state, fabs(cosmoV_readNumber(args[0])));
    return 1;
}

// math.floor
int cosmoB_mFloor(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "math.floor() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.floor", "<number>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    cosmoV_pushNumber(state, (int)cosmoV_readNumber(args[0]));
    return 1;
}

// math.ceil
int cosmoB_mCeil(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "math.ceil() expected 1 argument, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.ceil", "<number>", "%s", cosmoV_typeStr(args[0]));
        return 0;
    }

    int roundedDown = (int)cosmoV_readNumber(args[0]);

    // number is already truncated
    if ((double)roundedDown == cosmoV_readNumber(args[0])) {
        cosmoV_pushValue(state, args[0]);
    } else {
        cosmoV_pushNumber(state, roundedDown + 1);
    }

    return 1;
}

void cosmoB_loadMathLib(CState *state) {
    const char *identifiers[] = {
        "abs",
        "floor",
        "ceil"
    };

    CosmoCFunction mathLib[] = {
        cosmoB_mAbs,
        cosmoB_mFloor,
        cosmoB_mCeil
    };

    // make math library object
    cosmoV_pushString(state, "math");
    int i;
    for (i = 0; i < sizeof(identifiers)/sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, mathLib[i]);
    }

    // make the object and register it as a global to the state
    cosmoV_makeObject(state, i);
    cosmoV_register(state, 1);
}

// ================================================================ [DEBUG] ================================================================

int cosmoB_dsetProto(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        CObj *obj = cosmoV_readObj(args[0]); // object to set proto too
        CObjObject *proto = cosmoV_readObject(args[1]);

        obj->proto = proto; // boom done
    } else {
        cosmoV_error(state, "Expected 2 arguments, got %d!", nargs);
    }

    return 0; // nothing
}

int cosmoB_dgetProto(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 argument, got %d!", nargs);
        return 0;
    }

    cosmoV_pushObj(state, (CObj*)cosmoV_readObject(args[0])->_obj.proto); // just return the proto

    return 1; // 1 result
}

// ================================================================ [VM.*] ================================================================

// vm.__getter["globals"]
int cosmoB_vgetGlobal(CState *state, int nargs, CValue *args) {
    // this function doesn't need to check anything, just return the global table
    cosmoV_pushObj(state, (CObj*)state->globals);
    return 1;
}

// vm.__setter["globals"]
int cosmoB_vsetGlobal(CState *state, int nargs, CValue *args) {
    if (nargs != 2) {
        cosmoV_error(state, "Expected 2 argumenst, got %d!", nargs);
        return 0;
    }

    if (!IS_TABLE(args[1])) {
        cosmoV_typeError(state, "vm.__setter[\"globals\"]", "<object>, <table>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        return 0;
    }

    // this makes me very nervous ngl
    CObjTable *tbl = (CObjTable*)cosmoV_readObj(args[1]);
    state->globals = tbl;
    return 0;
}

int cosmoB_vindexBProto(CState *state, int nargs, CValue *args) {
    if (nargs != 2) {
        cosmoV_error(state, "Expected 2 arguments, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[1])) {
        cosmoV_typeError(state, "baseProtos.__index", "<object>, <number>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        return 0;
    }

    int indx = (int)cosmoV_readNumber(args[1]);

    if (indx >= COBJ_MAX || indx < 0) {
        cosmoV_error(state, "index out of range! expected 0 - %d, got %d!", COBJ_MAX - 1, indx);
        return 0;
    }

    if (state->protoObjects[indx] != NULL)
        cosmoV_pushObj(state, (CObj*)state->protoObjects[indx]);
    else
        cosmoV_pushNil(state);
    
    return 1; // 1 value pushed, 1 value returned
}

int cosmoB_vnewindexBProto(CState *state, int nargs, CValue *args) {
    if (nargs != 3) {
        cosmoV_error(state, "Expected 3 arguments, got %d!", nargs);
        return 0;
    }

    if (!IS_NUMBER(args[1]) || !IS_OBJECT(args[2])) {
        cosmoV_typeError(state, "baseProtos.__newindex", "<object>, <number>, <object>", "%s, %s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]), cosmoV_typeStr(args[2]));
        return 0;
    }

    int indx = (int)cosmoV_readNumber(args[1]);
    CObjObject *proto = cosmoV_readObject(args[2]);

    if (indx >= COBJ_MAX || indx < 0) {
        cosmoV_error(state, "index out of range! expected 0 - %d, got %d!", COBJ_MAX, indx);
        return 0;
    }

    cosmoV_registerProtoObject(state, indx, proto);
    return 0; // we don't return anything
}

// vm.collect()
int cosmoB_vcollect(CState *state, int nargs, CValue *args) {
    // first, unfreeze the state (we start frozen on entry to any C Function)
    cosmoM_unfreezeGC(state);
    
    // now force a garbage collection
    cosmoM_collectGarbage(state);

    // and re-freeze the state
    cosmoM_freezeGC(state);

    // the end!
    return 0;
}

void cosmoB_loadDebug(CState *state) {
    // make __getter object for debug proto
    cosmoV_pushString(state, "__getter");

    // key & value pair
    cosmoV_pushString(state, "__proto"); // key
    cosmoV_pushCFunction(state, cosmoB_dgetProto); // value

    cosmoV_makeTable(state, 1);

    // make __setter object
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "__proto");
    cosmoV_pushCFunction(state, cosmoB_dsetProto);

    cosmoV_makeTable(state, 1);

    // we call makeObject leting it know there are 2 sets of key & value pairs on the stack
    CObjObject *obj = cosmoV_makeObject(state, 2);

    // set debug protos to the debug object
    cosmoV_registerProtoObject(state, COBJ_OBJECT, obj);
    cosmoV_pop(state); // pops the debug object

    // make vm.* object
    cosmoV_pushString(state, "vm");

    // make vm.baseProtos object
    cosmoV_pushString(state, "baseProtos");

    cosmoV_pushString(state, "__index");
    cosmoV_pushCFunction(state, cosmoB_vindexBProto);

    cosmoV_pushString(state, "__newindex");
    cosmoV_pushCFunction(state, cosmoB_vnewindexBProto);

    cosmoV_makeObject(state, 2); // makes the baseProtos object

    // make __getter table for vm object
    cosmoV_pushString(state, "__getter");

    cosmoV_pushString(state, "globals");
    cosmoV_pushCFunction(state, cosmoB_vgetGlobal);

    cosmoV_makeTable(state, 1);

    // make __setter table for vm object
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "globals");
    cosmoV_pushCFunction(state, cosmoB_vsetGlobal);

    cosmoV_makeTable(state, 1);

    cosmoV_pushString(state, "collect");
    cosmoV_pushCFunction(state, cosmoB_vcollect);

    cosmoV_makeObject(state, 4); // makes the vm object

    // register "vm" to the global table
    cosmoV_register(state, 1);

    printf("[WARNING] the debug library has been loaded!\n");
}
