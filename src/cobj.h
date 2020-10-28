#ifndef COBJ_H
#define COBJ_H

#include "cosmo.h"
#include "cchunk.h"
#include "cvalue.h"

typedef struct CState CState;

typedef enum {
    COBJ_STRING,
    COBJ_UPVALUE,
    COBJ_FUNCTION,
    COBJ_CFUNCTION,
    COBJ_CLOSURE
} CObjType;

#define CommonHeader CObj obj;

typedef int (*CosmoCFunction)(CState *state, int argCount, CValue *args);

typedef struct CObj {
    CObjType type;
    bool isMarked; // for the GC
    struct CObj *next;
} CObj;

typedef struct CObjString {
    CommonHeader; // "is a" CObj
    int length;
    char *str;
    uint32_t hash; // for hashtable lookup
} CObjString;

typedef struct CObjUpval {
    CommonHeader; // "is a" CObj
    CValue *val;
    CValue closed;
    struct CObjUpval *next;
} CObjUpval;

typedef struct CObjFunction {
    CommonHeader; // "is a" CObj
    CChunk chunk;
    int args;
    int upvals;
    CObjString *name;
} CObjFunction;

typedef struct CObjCFunction {
    CommonHeader; // "is a" CObj
    CosmoCFunction cfunc;
} CObjCFunction;

typedef struct CObjClosure {
    CommonHeader;
    CObjFunction *function;
    CObjUpval **upvalues;
    int upvalueCount;
} CObjClosure;

#define IS_STRING(x)    isObjType(x, COBJ_STRING)
#define IS_FUNCTION(x)  isObjType(x, COBJ_FUNCTION)
#define IS_CFUNCTION(x) isObjType(x, COBJ_CFUNCTION)
#define IS_CLOSURE(x)   isObjType(x, COBJ_CLOSURE)

#define cosmoV_readString(x)    ((CObjString*)cosmoV_readObj(x))
#define cosmoV_readFunction(x)  ((CObjFunction*)cosmoV_readObj(x))
#define cosmoV_readCFunction(x) (((CObjCFunction*)cosmoV_readObj(x))->cfunc)
#define cosmoV_readClosure(x)   ((CObjClosure*)cosmoV_readObj(x))

static inline bool isObjType(CValue val, CObjType type) {
    return IS_OBJ(val) && cosmoV_readObj(val)->type == type;
}

CObj *cosmoO_allocateObject(CState *state, size_t sz, CObjType type);
void cosmoO_freeObject(CState *state, CObj* obj);

bool cosmoO_equalObject(CObj* obj1, CObj* obj2);

CObjFunction *cosmoO_newFunction(CState *state);
CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func);
CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func);
CObjString *cosmoO_toString(CState *state, CObj *val);
CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val);

// copies the *str buffer to the heap and returns a CObjString struct which is also on the heap
CObjString *cosmoO_copyString(CState *state, const char *str, size_t sz);
// pass an already allocated str buffer!
CObjString *cosmoO_takeString(CState *state, char *str, size_t sz);
// allocates a CObjStruct pointing directly to *str
CObjString *cosmoO_allocateString(CState *state, const char *str, size_t sz, uint32_t hash);

COSMO_API void printObject(CObj *o);

#define cosmoO_readCString(x)    ((CObjString*)x)->str

#endif