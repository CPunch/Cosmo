#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

void cosmoB_loadLibrary(CState *state) {
    cosmoM_freezeGC(state);
    cosmoV_register(state, "print", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_print)));
    cosmoM_unfreezeGC(state);
}

CValue cosmoB_print(CState *state, int nargs, CValue *args) {
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }
    printf("\n");

    return cosmoV_newNil(); // print doesn't return any args
}

CValue cosmoB_dsetProto(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        CObjObject *obj = cosmoV_readObject(args[0]); // object to set proto too
        CObjObject *proto = cosmoV_readObject(args[1]);

        obj->proto = proto; // boom done
    } else {
        cosmoV_error(state, "Expected 2 parameters, got %d!", nargs);
    }

    return cosmoV_newNil(); // nothing
}

CValue cosmoB_dgetProto(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 parameter, got %d!", nargs);
    }

    return cosmoV_newObj(cosmoV_readObject(args[0])->proto); // just return the proto
}

void cosmoB_loadDebug(CState *state) {
    cosmoV_pushString(state, "getProto");
    cosmoV_pushCFunction(state, cosmoB_dgetProto);
    cosmoV_pushString(state, "setProto");
    cosmoV_pushCFunction(state, cosmoB_dsetProto);
    cosmoV_pushObject(state, 2);

    state->protoObj = cosmoV_readObject(*cosmoV_pop(state));
}