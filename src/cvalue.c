#include "cvalue.h"

#include "cmem.h"
#include "cobj.h"
#include "cosmo.h"

void initValArray(CState *state, CValueArray *val, size_t startCapacity)
{
    val->count = 0;
    val->capacity = startCapacity;
    val->values = NULL;
}

void cleanValArray(CState *state, CValueArray *array)
{
    cosmoM_freeArray(state, CValue, array->values, array->capacity);
}

void appendValArray(CState *state, CValueArray *array, CValue val)
{
    cosmoM_growArray(state, CValue, array->values, array->count, array->capacity);

    array->values[array->count++] = val;
}

bool cosmoV_equal(CState *state, CValue valA, CValue valB)
{
    if (GET_TYPE(valA) != GET_TYPE(valB)) // are they the same type?
        return false;

    // compare
    switch (GET_TYPE(valA)) {
    case COSMO_TBOOLEAN:
        return cosmoV_readBoolean(valA) == cosmoV_readBoolean(valB);
    case COSMO_TNUMBER:
        return cosmoV_readNumber(valA) == cosmoV_readNumber(valB);
    case COSMO_TREF:
        return cosmoO_equal(state, cosmoV_readRef(valA), cosmoV_readRef(valB));
    case COSMO_TNIL:
        return true;
    default:
        return false;
    }
}

CObjString *cosmoV_toString(CState *state, CValue val)
{
    switch (GET_TYPE(val)) {
    case COSMO_TNUMBER: {
        char buf[32];
        int size = snprintf((char *)&buf, 32, "%.14g", cosmoV_readNumber(val));
        return cosmoO_copyString(state, (char *)&buf, size);
    }
    case COSMO_TBOOLEAN: {
        return cosmoV_readBoolean(val) ? cosmoO_copyString(state, "true", 4)
                                       : cosmoO_copyString(state, "false", 5);
    }
    case COSMO_TREF: {
        return cosmoO_toString(state, cosmoV_readRef(val));
    }
    case COSMO_TNIL: {
        return cosmoO_copyString(state, "nil", 3);
    }
    default:
        return cosmoO_copyString(state, "<unkn val>", 10);
    }
}

cosmo_Number cosmoV_toNumber(CState *state, CValue val)
{
    switch (GET_TYPE(val)) {
    case COSMO_TNUMBER: {
        return cosmoV_readNumber(val);
    }
    case COSMO_TBOOLEAN: {
        return cosmoV_readBoolean(val) ? 1 : 0;
    }
    case COSMO_TREF: {
        return cosmoO_toNumber(state, cosmoV_readRef(val));
    }
    case COSMO_TNIL: // fall through
    default:
        return 0;
    }
}

const char *cosmoV_typeStr(CValue val)
{
    switch (GET_TYPE(val)) {
    case COSMO_TNIL:
        return "<nil>";
    case COSMO_TBOOLEAN:
        return "<bool>";
    case COSMO_TNUMBER:
        return "<number>";
    case COSMO_TREF:
        return cosmoO_typeStr(cosmoV_readRef(val));

    default:
        return "<unkn val>";
    }
}

void cosmoV_printValue(CValue val)
{
    switch (GET_TYPE(val)) {
    case COSMO_TNUMBER:
        printf("%g", cosmoV_readNumber(val));
        break;
    case COSMO_TBOOLEAN:
        printf(cosmoV_readBoolean(val) ? "true" : "false");
        break;
    case COSMO_TREF: {
        printObject(cosmoV_readRef(val));
        break;
    }
    case COSMO_TNIL:
        printf("nil");
        break;
    default:
        printf("<unkn val>");
    }
}
