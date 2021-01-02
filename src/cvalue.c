#include "cosmo.h"
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
    if (GET_TYPE(valA) != GET_TYPE(valB)) // are they the same type?
        return false;

    // compare
    switch (GET_TYPE(valA)) {
        case COSMO_TBOOLEAN: return cosmoV_readBoolean(valA) == cosmoV_readBoolean(valB);
        case COSMO_TNUMBER: return cosmoV_readNumber(valA) == cosmoV_readNumber(valB);
        case COSMO_TOBJ: return cosmoO_equal(cosmoV_readObj(valA), cosmoV_readObj(valB));
        case COSMO_TNIL: return true;
        default:
            return false;
    }
}

CObjString *cosmoV_toString(CState *state, CValue val) {
    switch (GET_TYPE(val)) {
        case COSMO_TNUMBER: { 
            char buf[32];
            int size = snprintf((char*)&buf, 32, "%.14g", cosmoV_readNumber(val));
            return cosmoO_copyString(state, (char*)&buf, size);
        }
        case COSMO_TBOOLEAN: {
            return cosmoV_readBoolean(val) ? cosmoO_copyString(state, "true", 4) : cosmoO_copyString(state, "false", 5);            
        }
        case COSMO_TOBJ: {
            return cosmoO_toString(state, cosmoV_readObj(val));
        }
        case COSMO_TNIL: {
            return cosmoO_copyString(state, "nil", 3); 
        }
        default:
            return cosmoO_copyString(state, "<unkn val>", 10);
    }
}

const char *cosmoV_typeStr(CValue val) {
    switch (GET_TYPE(val)) {
        case COSMO_TNIL:        return "<nil>";
        case COSMO_TBOOLEAN:    return "<bool>";
        case COSMO_TNUMBER:     return "<number>";
        case COSMO_TOBJ:        return cosmoO_typeStr(cosmoV_readObj(val));
        
        default:
            return "<unkn val>";
    }
}

void printValue(CValue val) {
    switch (GET_TYPE(val)) {
        case COSMO_TNUMBER:
            printf("%g", cosmoV_readNumber(val));
            break;
        case COSMO_TBOOLEAN:
            printf(cosmoV_readBoolean(val) ? "true" : "false");
            break;
        case COSMO_TOBJ: {
            printObject(cosmoV_readObj(val));
            break;
        }
        case COSMO_TNIL:
            printf("nil");
            break;
        default:
            printf("<unkn val>");
    }
}
