#include "cmem.h"
#include "cvalue.h"
#include "cobj.h"

void initValArray(CState *state, CValueArray *val, size_t startCapacity) {
    val->count = 0;
    val->capacity = startCapacity;
    val->values = NULL;
}

void cleanValArray(CState *state, CValueArray *array) {
    cosmoM_freearray(state, CValue, array->values, array->capacity);
}

void appendValArray(CState *state, CValueArray *array, CValue val) {
    cosmoM_growarray(state, CValue, array->values, array->count, array->capacity);

    array->values[array->count++] = val;
}

bool cosmoV_equal(CValue valA, CValue valB) {
    if (valA.type != valB.type) // are they the same type?
        return false;

    // compare
    switch (valA.type) {
        case COSMO_TBOOLEAN: return valA.val.b == valB.val.b;
        case COSMO_TNUMBER: return valA.val.num == valB.val.num;
        case COSMO_TOBJ: return cosmoO_equal(valA.val.obj, valB.val.obj);
        case COSMO_TNIL: return true;
        default:
            return false;
    }
}

CObjString *cosmoV_toString(CState *state, CValue val) {
    switch (val.type) {
        case COSMO_TNUMBER: { 
            char buf[32];
            int size = snprintf((char*)&buf, 32, "%.14g", val.val.num);
            return cosmoO_copyString(state, (char*)&buf, size);
        }
        case COSMO_TBOOLEAN: {
            return val.val.b ? cosmoO_copyString(state, "true", 4) : cosmoO_copyString(state, "false", 5);            
        }
        case COSMO_TOBJ: {
            return cosmoO_toString(state, val.val.obj);
        }
        case COSMO_TNIL: {
            return cosmoO_copyString(state, "nil", 3); 
        }
        default:
            return cosmoO_copyString(state, "<unkn val>", 10);
    }
}

const char *cosmoV_typeStr(CValue val) {
    switch (val.type) {
        case COSMO_TNIL:        return "<nil>";
        case COSMO_TBOOLEAN:    return "<bool>";
        case COSMO_TNUMBER:     return "<number>";
        case COSMO_TOBJ:        return cosmoO_typeStr(val.val.obj);
        
        default:
            return "<unkn val>";
    }
}

void printValue(CValue val) {
    switch (val.type) {
        case COSMO_TNUMBER:
            printf("%g", val.val.num);
            break;
        case COSMO_TBOOLEAN:
            printf(cosmoV_readBoolean(val) ? "true" : "false");
            break;
        case COSMO_TOBJ: {
            printObject(val.val.obj);
            break;
        }
        case COSMO_TNIL:
            printf("nil");
            break;
        default:
            printf("<unkn val>");
    }
}