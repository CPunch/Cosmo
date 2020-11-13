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

CValue cosmoB_dsetMeta(CState *state, int nargs, CValue *args) {
    if (nargs == 2) {
        CObjObject *obj = cosmoV_readObject(args[0]); // object to set meta too
        CObjObject *meta = cosmoV_readObject(args[1]);

        obj->meta = meta; // boom done
    } else {
        cosmoV_error(state, "Expected 2 parameters, got %d!", nargs);
    }

    return cosmoV_newNil(); // nothing
}
CValue cosmoB_dgetMeta(CState *state, int nargs, CValue *args) {
    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 parameter, got %d!", nargs);
    }

    return cosmoV_newObj(cosmoV_readObject(args[0])->meta); // just return the meta
}

void cosmoB_loadDebug(CState *state) {
    cosmoV_pushString(state, "getMeta");
    cosmoV_pushCFunction(state, cosmoB_dgetMeta);
    cosmoV_pushString(state, "setMeta");
    cosmoV_pushCFunction(state, cosmoB_dsetMeta);
    cosmoV_pushObject(state, 2);

    state->metaObj = cosmoV_readObject(*cosmoV_pop(state));
}