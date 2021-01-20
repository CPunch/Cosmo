#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

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

    // call the passed callable
    COSMOVMRESULT res = cosmoV_pcall(state, nargs-1, 1);

    // insert false before the result
    cosmo_insert(state, 0, cosmoV_newBoolean(res == COSMOVM_OK));
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

    cosmoV_pushValue(state, cosmoV_newObj(cosmoV_toString(state, args[0])));
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

    // load object libraries
    cosmoB_loadStrLib(state);
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

// string.charAt
int cosmoB_sCharAt(CState *state, int nargs, CValue *args) {
    if (nargs != 2) {
        cosmoV_error(state, "string.charAt() expected 2 arguments, got %d!", nargs);
        return 0;
    }

    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        cosmoV_typeError(state, "string.charAt", "<string>, <number>", "%s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        return 0;
    }

    CObjString *str = cosmoV_readString(args[0]);
    int indx = (int)cosmoV_readNumber(args[1]);

    if (indx >= str->length || indx < 0) {
        cosmoV_error(state, "string.charAt() expected index to be 0-%d, got %d!", str->length - 1, indx);
        return 0;
    }

    // returns character number
    cosmoV_pushNumber(state, (int)str->str[indx]);
    return 1;
}

void cosmoB_loadStrLib(CState *state) {
    const char *identifiers[] = {
        "sub",
        "find",
        "split",
        "charAt"
    };

    CosmoCFunction strLib[] = {
        cosmoB_sSub,
        cosmoB_sFind,
        cosmoB_sSplit,
        cosmoB_sCharAt
    };


    // make string library object
    cosmoV_pushString(state, "string");
    int i;
    for (i = 0; i < sizeof(identifiers)/sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, strLib[i]);
    }
    cosmoV_makeObject(state, i);

    // grab the object from the stack and set the base protoObject
    StkPtr obj = cosmoV_getTop(state, 0);
    state->protoObjects[COBJ_STRING] = cosmoV_readObject(*obj);

    // register "string" to the global table
    cosmoV_register(state, 1);
}

// ================================================================ [DEBUG] ================================================================

int cosmoB_dsetProto(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        CObj *obj = cosmoV_readObj(args[0]); // object to set proto too
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

    cosmoV_pushValue(state, cosmoV_newObj(cosmoV_readObject(args[0])->_obj.proto)); // just return the proto

    return 1; // 1 result
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
    cosmoV_makeObject(state, 2);

    // set debug protos to the debug object
    state->protoObjects[COBJ_OBJECT] = cosmoV_readObject(*cosmoV_pop(state));
}
