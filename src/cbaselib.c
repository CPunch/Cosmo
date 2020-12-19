#include "cbaselib.h"
#include "cvm.h"
#include "cvalue.h"
#include "cobj.h"
#include "cmem.h"

void cosmoB_loadLibrary(CState *state) {
    cosmoM_freezeGC(state);
    cosmoV_register(state, "print", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_print)));
    cosmoV_register(state, "foreach", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_foreach)));
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

int cosmoB_foreach(CState *state, int nargs, CValue *args) {
    if (nargs != 2) {
        cosmoV_error(state, "foreach() expected 2 parameters, got %d!", nargs);
        return 0;
    }

    if (!IS_DICT(args[0])) {
        cosmoV_error(state, "foreach() expected first parameter to be <dictionary>, got %s!", cosmoV_typeStr(args[0]));
        return 0;
    }

    if (!IS_CALLABLE(args[1])) {
        cosmoV_error(state, "foreach() expected second parameter to be callable, got %s!", cosmoV_typeStr(args[1]));
        return 0;
    }

    // loop through dictionary table, calling args[1] on active entries
    CObjDict *dict = (CObjDict*)cosmoV_readObj(args[0]);
    
    for (int i = 0; i < dict->tbl.capacity; i++) {
        CTableEntry *entry = &dict->tbl.table[i];

        if (!IS_NIL(entry->key)) {
            // push key & value, then call args[1]
            cosmoV_pushValue(state, args[1]);
            cosmoV_pushValue(state, entry->key);
            cosmoV_pushValue(state, entry->val);
            cosmoV_call(state, 2, 0);
        }
    }

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
        cosmoV_error(state, "Expected 1 parameter, got %d!", nargs);
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