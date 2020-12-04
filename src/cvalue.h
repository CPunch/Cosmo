#ifndef CVALUE_H
#define CVALUE_H

#include "cosmo.h"

typedef enum {
    COSMO_TNUMBER, // number has to be 0 because NaN box
    COSMO_TBOOLEAN,
    COSMO_TOBJ,
    COSMO_TNIL,
} CosmoType;

typedef double cosmo_Number;

/*
    holds primitive cosmo types
*/

#ifdef NAN_BOXXED
/*
        NaN box, this is great for performance on x86_64 or ARM64 architectures. If you don't know how this works please reference these
    two articles:

    https://leonardschuetz.ch/blog/nan-boxing/ and https://piotrduperas.com/posts/nan-boxing/

    both are great resources :)

    TL;DR: we can store payloads in the NaN value in the IEEE 754 standard.
*/
typedef union CValue {
    uint64_t data;
    cosmo_Number num;
} CValue;

#define MASK_SIGN_BIT   ((uint64_t)1 << 63)
#define MASK_TYPE       ((uint64_t)0xf)
#define MASK_PAYLOAD    ((uint64_t)0xfffffffffff0)

#define MAKE_PAYLOAD(x) ((uint64_t)(x) << 4)

// The bits that must be set to indicate a quiet NaN.
#define MASK_QUIETNAN        ((uint64_t)0x7ffc000000000000)

// sadly this requires a bit more than a simple macro :(
static inline CosmoType GET_TYPE(CValue val) {
    // it's not not a number (its a number)
    if ((((val.data) & MASK_QUIETNAN) != MASK_QUIETNAN))
        return COSMO_TNUMBER;

    if ((((val.data) & (MASK_QUIETNAN | MASK_SIGN_BIT)) == (MASK_QUIETNAN | MASK_SIGN_BIT)))
        return COSMO_TOBJ;

    return (val.data & MASK_TYPE);
}

#define cosmoV_newNumber(x)     ((CValue){.num = x})
#define cosmoV_newObj(x)        ((CValue){.data = MASK_SIGN_BIT | MASK_QUIETNAN | (uint64_t)(uintptr_t)(x)})
#define cosmoV_newBoolean(x)    ((CValue){.data = MASK_QUIETNAN | MAKE_PAYLOAD(x) | COSMO_TBOOLEAN})
#define cosmoV_newNil()         ((CValue){.data = MASK_QUIETNAN | COSMO_TNIL})

#define cosmoV_readNumber(x)    ((x).num)
#define cosmoV_readBoolean(x)   ((bool)((x).data & MASK_PAYLOAD))
#define cosmoV_readObj(x)       (((CObj*)(uintptr_t)(((x).data) & ~(MASK_SIGN_BIT | MASK_QUIETNAN))))

#else
/*
    Tagged union, this is the best platform independent solution
*/
typedef struct CValue {
    CosmoType type;
    union {
        cosmo_Number num;
        bool b; // boolean
        CObj *obj;
    } val;
} CValue;

#define GET_TYPE(x)     ((x).type)

// create CValues

#define cosmoV_newNumber(x)     ((CValue){COSMO_TNUMBER,    {.num = (x)}})
#define cosmoV_newBoolean(x)    ((CValue){COSMO_TBOOLEAN,   {.b = (x)}})
#define cosmoV_newObj(x)        ((CValue){COSMO_TOBJ,       {.obj = (CObj*)(x)}})
#define cosmoV_newNil()         ((CValue){COSMO_TNIL,       {.num = 0}})

// read CValues

#define cosmoV_readNumber(x)    ((cosmo_Number)(x).val.num)
#define cosmoV_readBoolean(x)   ((bool)(x).val.b)
#define cosmoV_readObj(x)       ((CObj*)(x).val.obj)

#endif

#define IS_NUMBER(x)    (GET_TYPE(x) == COSMO_TNUMBER)
#define IS_BOOLEAN(x)   (GET_TYPE(x) == COSMO_TBOOLEAN)
#define IS_NIL(x)       (GET_TYPE(x) == COSMO_TNIL)
#define IS_OBJ(x)       (GET_TYPE(x) == COSMO_TOBJ)

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
COSMO_API const char *cosmoV_typeStr(CValue val); // return constant char array for corresponding type

#endif