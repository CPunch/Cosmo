#ifndef COBJ_H
#define COBJ_H

#include "cosmo.h"

typedef enum {
    COBJ_STRING,
    COBJ_OBJECT,
    COBJ_DICT, // dictionary
    COBJ_FUNCTION,
    COBJ_CFUNCTION,
    COBJ_ERROR,
    // internal use
    COBJ_METHOD,
    COBJ_CLOSURE,
    COBJ_UPVALUE,
    COBJ_MAX
} CObjType;

#include "cstate.h"
#include "cchunk.h"
#include "cvalue.h"
#include "ctable.h"

typedef struct CState CState;
typedef struct CCallFrame CCallFrame;
typedef uint32_t cosmo_Flag;

#define CommonHeader CObj _obj
#define readFlag(x, flag)   (x & (1u << flag))
#define setFlagOn(x, flag)  (x |= (1u << flag))

typedef int (*CosmoCFunction)(CState *state, int argCount, CValue *args);

typedef struct CObj {
    CObjType type;
    bool isMarked; // for the GC
    struct CObj *next;
    struct CObj *nextRoot; // for the root linked list
    struct CObjObject *proto; // protoobject, describes the behavior of the object
} CObj;

typedef struct CObjString {
    CommonHeader; // "is a" CObj
    bool isIString;
    int length;
    char *str;
    uint32_t hash; // for hashtable lookup
} CObjString;

typedef struct CObjError {
    CommonHeader; // "is a" CObj
    bool parserError; // if true, cosmoV_printError will format the error to the lexer
    int frameCount;
    int line; // reserved for parser errors
    CValue err; // error string
    CCallFrame *frames;
} CObjError;

typedef struct CObjObject {
    CommonHeader; // "is a" CObj
    cosmo_Flag istringFlags; // enables us to have a much faster lookup for reserved IStrings (like __init, __index, etc.)
    CTable tbl;
    union { // userdata (NULL by default)
        void *userP;
        int userI;
    };
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
    bool variadic;
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
    CObj *obj; // obj this method is bound too
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
#define IS_DICT(x)      isObjType(x, COBJ_DICT)
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

// just protects against macro expansion
static inline bool IS_CALLABLE(CValue val) {
    return IS_CLOSURE(val) || IS_CFUNCTION(val) || IS_METHOD(val);
}  

CObj *cosmoO_allocateBase(CState *state, size_t sz, CObjType type);
void cosmoO_free(CState *state, CObj* obj);

bool cosmoO_equal(CObj* obj1, CObj* obj2);

CObjObject *cosmoO_newObject(CState *state);
CObjDict *cosmoO_newDictionary(CState *state);
CObjFunction *cosmoO_newFunction(CState *state);
CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func);
CObjError *cosmoO_newError(CState *state, CValue err);
CObjMethod *cosmoO_newMethod(CState *state, CValue func, CObj *obj);
CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func);
CObjString *cosmoO_toString(CState *state, CObj *val);
CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val);

// grabs the base proto of the CObj* (if CObj is a CObjObject, that is returned)
static inline CObjObject *cosmoO_grabProto(CObj *obj) {
    CObjObject *object = obj->proto;

    if (obj->type == COBJ_OBJECT)
        object = (CObjObject*)obj;
    
    return object;
}

bool cosmoO_getRawObject(CState *state, CObjObject *object, CValue key, CValue *val);
void cosmoO_setRawObject(CState *state, CObjObject *object, CValue key, CValue val);
bool cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val);
bool cosmoO_newIndexObject(CState *state, CObjObject *object, CValue key, CValue val);

void cosmoO_setUserP(CState *state, CObjObject *object, void *p);
void *cosmoO_getUserP(CState *state, CObjObject *object);
void cosmoO_setUserI(CState *state, CObjObject *object, int i);
int cosmoO_getUserI(CState *state, CObjObject *object);

// internal string
bool cosmoO_getIString(CState *state, CObjObject *object, int flag, CValue *val);

// copies the *str buffer to the heap and returns a CObjString struct which is also on the heap
CObjString *cosmoO_copyString(CState *state, const char *str, size_t sz);
// pass an already allocated str buffer!
CObjString *cosmoO_takeString(CState *state, char *str, size_t sz);
// allocates a CObjStruct pointing directly to *str
CObjString *cosmoO_allocateString(CState *state, const char *str, size_t sz, uint32_t hash);

/*
    formats strings to push onto the VM stack, formatting supported:

    '%d' - decimal numbers  [int]
    '%f' - floating point   [double]
    '%s' - strings          [const char*]
    '%t' - cosmo tokens     [CToken *]
*/
CObjString *cosmoO_pushVFString(CState *state, const char *format, va_list args);

COSMO_API void printObject(CObj *o);
const char *cosmoO_typeStr(CObj* obj);

#define cosmoO_readCString(x)    ((CObjString*)x)->str

#endif
