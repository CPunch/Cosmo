#ifndef COBJ_H
#define COBJ_H

#include "cosmo.h"
#include "cstate.h"
#include "cchunk.h"
#include "cvalue.h"
#include "ctable.h"

typedef struct CState CState;
typedef uint32_t cosmo_Flag;

typedef enum {
    COBJ_STRING,
    COBJ_OBJECT,
    COBJ_DICT, // dictionary
    COBJ_FUNCTION,
    COBJ_CFUNCTION,
    // internal use
    COBJ_METHOD,
    COBJ_CLOSURE,
    COBJ_UPVALUE,
} CObjType;

#define CommonHeader CObj _obj;
#define readFlag(x, flag)   (x & (1u << flag))
#define setFlagOn(x, flag)  (x |= (1u << flag))

typedef CValue (*CosmoCFunction)(CState *state, int argCount, CValue *args);

typedef struct CObj {
    CObjType type;
    bool isMarked; // for the GC
    struct CObj *next;
    struct CObj *nextRoot; // for the root linked list
} CObj;

typedef struct CObjString {
    CommonHeader; // "is a" CObj
    bool isIString;
    int length;
    char *str;
    uint32_t hash; // for hashtable lookup
} CObjString;

typedef struct CObjObject {
    CommonHeader; // "is a" CObj
    cosmo_Flag istringFlags; // enables us to have a much faster lookup for reserved IStrings (like __init, __index, etc.)
    CTable tbl;
    void *user; // userdata (NULL by default)
    struct CObjObject *proto; // protoobject, describes the behavior of the object
} CObjObject;

typedef struct CObjDict { // dictionary, a wrapper for CTable
    CommonHeader; // "is a" CObj
    CTable tbl;
} CObjDict;

typedef struct CObjFunction {
    CommonHeader; // "is a" CObj
    CChunk chunk;
    int args;
    int upvals;
    CObjString *name;
    CObjString *module; // name of the "module"
} CObjFunction;

typedef struct CObjCFunction {
    CommonHeader; // "is a" CObj
    CosmoCFunction cfunc;
} CObjCFunction;

typedef struct CObjClosure {
    CommonHeader; // "is a" CObj
    CObjFunction *function;
    CObjUpval **upvalues;
    int upvalueCount;
} CObjClosure;

typedef struct CObjMethod {
    CommonHeader; // "is a " CObj
    CObjObject *obj; // obj this method is bound too
    CValue func;
} CObjMethod;

typedef struct CObjUpval {
    CommonHeader; // "is a" CObj
    CValue *val;
    CValue closed;
    struct CObjUpval *next;
} CObjUpval;

#undef CommonHeader

#define IS_STRING(x)    isObjType(x, COBJ_STRING)
#define IS_OBJECT(x)    isObjType(x, COBJ_OBJECT)
#define IS_FUNCTION(x)  isObjType(x, COBJ_FUNCTION)
#define IS_CFUNCTION(x) isObjType(x, COBJ_CFUNCTION)
#define IS_METHOD(x)    isObjType(x, COBJ_METHOD)
#define IS_CLOSURE(x)   isObjType(x, COBJ_CLOSURE)

#define cosmoV_readString(x)    ((CObjString*)cosmoV_readObj(x))
#define cosmoV_readObject(x)    ((CObjObject*)cosmoV_readObj(x))
#define cosmoV_readFunction(x)  ((CObjFunction*)cosmoV_readObj(x))
#define cosmoV_readCFunction(x) (((CObjCFunction*)cosmoV_readObj(x))->cfunc)
#define cosmoV_readMethod(x)    ((CObjMethod*)cosmoV_readObj(x))
#define cosmoV_readClosure(x)   ((CObjClosure*)cosmoV_readObj(x))

static inline bool isObjType(CValue val, CObjType type) {
    return IS_OBJ(val) && cosmoV_readObj(val)->type == type;
}

CObj *cosmoO_allocateBase(CState *state, size_t sz, CObjType type);
void cosmoO_free(CState *state, CObj* obj);

bool cosmoO_equal(CObj* obj1, CObj* obj2);

CObjObject *cosmoO_newObject(CState *state);
CObjDict *cosmoO_newDictionary(CState *state);
CObjFunction *cosmoO_newFunction(CState *state);
CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func);
CObjMethod *cosmoO_newMethod(CState *state, CObjClosure *func, CObjObject *obj);
CObjMethod *cosmoO_newCMethod(CState *state,  CObjCFunction *func, CObjObject *obj);
CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func);
CObjString *cosmoO_toString(CState *state, CObj *val);
CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val);

bool cosmoO_getObject(CState *state, CObjObject *object, CValue key, CValue *val);
void cosmoO_setObject(CState *state, CObjObject *object, CValue key, CValue val);
bool cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val);
bool cosmoO_newIndexObject(CState *state, CObjObject *object, CValue key, CValue val);

void cosmoO_setUserData(CState *state, CObjObject *object, void *p);
void *cosmoO_getUserData(CState *state, CObjObject *object);

// internal string
bool cosmoO_getIString(CState *state, CObjObject *object, int flag, CValue *val);

// copies the *str buffer to the heap and returns a CObjString struct which is also on the heap
CObjString *cosmoO_copyString(CState *state, const char *str, size_t sz);
// pass an already allocated str buffer!
CObjString *cosmoO_takeString(CState *state, char *str, size_t sz);
// allocates a CObjStruct pointing directly to *str
CObjString *cosmoO_allocateString(CState *state, const char *str, size_t sz, uint32_t hash);

COSMO_API void printObject(CObj *o);
const char *cosmoO_typeStr(CObj* obj);

#define cosmoO_readCString(x)    ((CObjString*)x)->str

#endif