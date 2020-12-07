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
        NaN box, this is great for fitting more in the cpu cache on x86_64 or ARM64 architectures. If you don't know how this works please reference these
    two articles:

    https://leonardschuetz.ch/blog/nan-boxing/ and https://piotrduperas.com/posts/nan-boxing/

    both are great resources :)

    Performance notes: this can actually degrade performance, so only enable if you know what you're doing.

    TL;DR: we can store payloads in the NaN value in the IEEE 754 standard.
*/
typedef union CValue {
    uint64_t data;
    cosmo_Number num;
} CValue;

#define MASK_TYPE       ((uint64_t)0x7)
#define MASK_PAYLOAD    ((uint64_t)0x0007fffffffffff8)

// 3 bits (low bits) are reserved for the type
#define MAKE_PAYLOAD(x) (((uint64_t)(x) << 3) & MASK_PAYLOAD)
#define READ_PAYLOAD(x) (((x).data & MASK_PAYLOAD) >> 3)

// The bits that must be set to indicate a quiet NaN.
#define MASK_QUIETNAN   ((uint64_t)0x7ff8000000000000)

#define GET_TYPE(x) \
    ((((x).data & MASK_QUIETNAN) == MASK_QUIETNAN) ? ((x).data & MASK_TYPE) : COSMO_TNUMBER)

#define SIG_MASK        (MASK_QUIETNAN | MASK_TYPE)
#define BOOL_SIG        (MASK_QUIETNAN | COSMO_TBOOLEAN)
#define OBJ_SIG         (MASK_QUIETNAN | COSMO_TOBJ)
#define NIL_SIG         (MASK_QUIETNAN | COSMO_TNIL)

#define cosmoV_newNumber(x)     ((CValue){.num = x})
#define cosmoV_newObj(x)        ((CValue){.data = MASK_QUIETNAN | MAKE_PAYLOAD((uintptr_t)x) | COSMO_TOBJ})
#define cosmoV_newBoolean(x)    ((CValue){.data = MASK_QUIETNAN | MAKE_PAYLOAD(x) | COSMO_TBOOLEAN})
#define cosmoV_newNil()         ((CValue){.data = MASK_QUIETNAN | COSMO_TNIL})

#define cosmoV_readNumber(x)    ((x).num)
#define cosmoV_readBoolean(x)   ((bool)READ_PAYLOAD(x))
#define cosmoV_readObj(x)       ((CObj*)READ_PAYLOAD(x))

#define IS_NUMBER(x)    (((x).data & MASK_QUIETNAN) != MASK_QUIETNAN)
#define IS_BOOLEAN(x)   (((x).data & SIG_MASK) == BOOL_SIG)
#define IS_NIL(x)       (((x).data & SIG_MASK) == NIL_SIG)
#define IS_OBJ(x)       (((x).data & SIG_MASK) == OBJ_SIG)

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

#define IS_NUMBER(x)    (GET_TYPE(x) == COSMO_TNUMBER)
#define IS_BOOLEAN(x)   (GET_TYPE(x) == COSMO_TBOOLEAN)
#define IS_NIL(x)       (GET_TYPE(x) == COSMO_TNIL)
#define IS_OBJ(x)       (GET_TYPE(x) == COSMO_TOBJ)

#endif

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