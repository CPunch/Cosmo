#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

void cosmoB_loadLibrary(CState *state) {
    // print
    cosmoV_pushString(state, "print");
    cosmoV_pushCFunction(state, cosmoB_print);

    // assert (for unit testing)
    cosmoV_pushString(state, "assert");
    cosmoV_pushCFunction(state, cosmoB_assert);

    cosmoV_register(state, 2);
}

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
        cosmoV_error(state, "assert() expected <boolean>, got %s!", cosmoV_typeStr(args[0]));
        return 0;
    }

    if (!cosmoV_readBoolean(args[0])) { // expression passed was false, error!
        cosmoV_error(state, "assert() failed!");
    } // else do nothing :)

    return 0;
}

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

    cosmoV_makeObject(state, 1);

    // make __setter object
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "__proto");
    cosmoV_pushCFunction(state, cosmoB_dsetProto);

    cosmoV_makeObject(state, 1);

    // we call makeObject leting it know there are 2 sets of key & value pairs on the stack
    cosmoV_makeObject(state, 2);

    // set debug proto to the debug object
    state->protoObj = cosmoV_readObject(*cosmoV_pop(state));
}