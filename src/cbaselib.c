#include "cbaselib.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

void cosmoB_loadlibrary(CState *state) {
    cosmoM_freezeGC(state);
    cosmoV_register(state, "print", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_print)));
    cosmoM_unfreezeGC(state);
}

int cosmoB_print(CState *state, int nargs, CValue *args) {
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }

    printf("\n");

    return 0; // print doesn't return any args
}