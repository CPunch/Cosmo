#ifndef CVALUE_H
#define CVALUE_H

#include "cosmo.h"

typedef enum {
    COSMO_TNIL,
    COSMO_TBOOLEAN,
    COSMO_TNUMBER,
    COSMO_TOBJ,
    COSMO_TUSERDATA
} CosmoType;

typedef double cosmo_Number;

/*
    holds primitive cosmo types
*/
typedef struct CValue {
    CosmoType type;
    union {
        cosmo_Number num;
        bool b; // boolean
        void *ptr; // userdata
        CObj *obj;
    } val;
} CValue;
typedef CValue* StkPtr;

typedef struct CValueArray {
    size_t capacity;
    size_t count;
    CValue *values;
} CValueArray;

void initValArray(CState *state, CValueArray *val, size_t startCapacity);
void cleanValArray(CState *state, CValueArray *array); // cleans array
void appendValArray(CState *state, CValueArray *array, CValue val);

void printValue(CValue val);
COSMO_API bool cosmoV_equal(CValue valA, CValue valB);
COSMO_API CObjString *cosmoV_toString(CState *state, CValue val);

#define IS_NUMBER(x)    x.type == COSMO_TNUMBER
#define IS_BOOLEAN(x)   x.type == COSMO_TBOOLEAN
#define IS_NIL(x)       x.type == COSMO_TNIL
#define IS_OBJ(x)       x.type == COSMO_TOBJ

// create CValues

#define cosmoV_newNumber(x)     ((CValue){COSMO_TNUMBER, {.num = x}})
#define cosmoV_newBoolean(x)    ((CValue){COSMO_TBOOLEAN, {.b = x}})
#define cosmoV_newObj(x)        ((CValue){COSMO_TOBJ, {.obj = (CObj*)x}})
#define cosmoV_newNil()         ((CValue){COSMO_TNIL, {.num = 0}})

// read CValues

#define cosmoV_readNumber(x)    ((cosmo_Number)x.val.num)
#define cosmoV_readBoolean(x)   ((bool)x.val.b)
#define cosmoV_readObj(x)       ((CObj*)x.val.obj)

#endif