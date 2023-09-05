#ifndef COBJ_H
#define COBJ_H

#include "cosmo.h"

#include <stdarg.h>

typedef enum CObjType
{
    COBJ_STRING,
    COBJ_OBJECT,
    COBJ_TABLE,
    COBJ_FUNCTION,
    COBJ_CFUNCTION,
    // internal use
    COBJ_ERROR,
    COBJ_METHOD,
    COBJ_CLOSURE,
    COBJ_UPVALUE,
    COBJ_MAX
} CObjType;

#include "cchunk.h"
#include "cstate.h"
#include "ctable.h"
#include "cvalue.h"

#define CommonHeader       CObj _obj
#define readFlag(x, flag)  (x & (1u << flag))
#define setFlagOn(x, flag) (x |= (1u << flag))

typedef int (*CosmoCFunction)(CState *state, int argCount, CValue *args);

struct CObj
{
    struct CObj *next;
    struct CObjObject *proto; // protoobject, describes the behavior of the object
    CObjType type;
    bool isMarked; // for the GC
};

struct CObjString
{
    CommonHeader;  // "is a" CObj
    char *str;     // NULL terminated string
    uint32_t hash; // for hashtable lookup
    int length;
    bool isIString;
};

struct CObjError
{
    CommonHeader; // "is a" CObj
    CValue err;   // error string
    CCallFrame *frames;
    int frameCount;
    int line;         // reserved for parser errors
    bool parserError; // if true, cosmoV_printError will format the error to the lexer
};

struct CObjObject
{
    CommonHeader; // "is a" CObj
    CTable tbl;
    cosmo_Flag istringFlags; // enables us to have a much faster lookup for reserved IStrings (like
                             // __init, __index, etc.)
    union
    { // userdata (NULL by default)
        void *userP;
        int userI;
    };
    int userT; // user-defined type (for describing the userdata pointer/integer)
    bool isLocked;
};

struct CObjTable
{                 // table, a wrapper for CTable
    CommonHeader; // "is a" CObj
    CTable tbl;
};

struct CObjFunction
{
    CommonHeader; // "is a" CObj
    CChunk chunk;
    CObjString *name;
    CObjString *module; // name of the "module"
    int args;
    int upvals;
    bool variadic;
};

struct CObjCFunction
{
    CommonHeader; // "is a" CObj
    CosmoCFunction cfunc;
};

struct CObjClosure
{
    CommonHeader; // "is a" CObj
    CObjFunction *function;
    CObjUpval **upvalues;
    int upvalueCount;
};

struct CObjMethod
{
    CommonHeader; // "is a " CObj
    CValue func;
    CObj *obj; // obj this method is bound too
};

struct CObjUpval
{
    CommonHeader; // "is a" CObj
    CValue closed;
    CValue *val;
    struct CObjUpval *next;
};

#undef CommonHeader

#define IS_STRING(x)            isObjType(x, COBJ_STRING)
#define IS_OBJECT(x)            isObjType(x, COBJ_OBJECT)
#define IS_TABLE(x)             isObjType(x, COBJ_TABLE)
#define IS_FUNCTION(x)          isObjType(x, COBJ_FUNCTION)
#define IS_CFUNCTION(x)         isObjType(x, COBJ_CFUNCTION)
#define IS_METHOD(x)            isObjType(x, COBJ_METHOD)
#define IS_CLOSURE(x)           isObjType(x, COBJ_CLOSURE)

#define cosmoV_readString(x)    ((CObjString *)cosmoV_readRef(x))
#define cosmoV_readCString(x)   (((CObjString *)cosmoV_readRef(x))->str)
#define cosmoV_readObject(x)    ((CObjObject *)cosmoV_readRef(x))
#define cosmoV_readTable(x)     ((CObjTable *)cosmoV_readRef(x))
#define cosmoV_readFunction(x)  ((CObjFunction *)cosmoV_readRef(x))
#define cosmoV_readCFunction(x) (((CObjCFunction *)cosmoV_readRef(x))->cfunc)
#define cosmoV_readMethod(x)    ((CObjMethod *)cosmoV_readRef(x))
#define cosmoV_readClosure(x)   ((CObjClosure *)cosmoV_readRef(x))
#define cosmoV_readError(x)     ((CObjError *)cosmoV_readRef(x))

#define cosmoO_readCString(x)   ((CObjString *)x)->str
#define cosmoO_readType(x)      ((CObj *)x)->type

static inline bool isObjType(CValue val, CObjType type)
{
    return IS_REF(val) && cosmoV_readRef(val)->type == type;
}

static inline bool IS_CALLABLE(CValue val)
{
    return IS_CLOSURE(val) || IS_CFUNCTION(val) || IS_METHOD(val);
}

void cosmoO_free(CState *state, CObj *obj);
bool cosmoO_equal(CState *state, CObj *obj1, CObj *obj2);

// walks the protos of obj and checks for proto
bool cosmoO_isDescendant(CObj *obj, CObjObject *proto);

CObjObject *cosmoO_newObject(CState *state);
CObjTable *cosmoO_newTable(CState *state);
CObjFunction *cosmoO_newFunction(CState *state);
CObjCFunction *cosmoO_newCFunction(CState *state, CosmoCFunction func);
CObjError *cosmoO_newError(CState *state, CValue err);
CObjMethod *cosmoO_newMethod(CState *state, CValue func, CObj *obj);
CObjClosure *cosmoO_newClosure(CState *state, CObjFunction *func);
CObjUpval *cosmoO_newUpvalue(CState *state, CValue *val);

// grabs the base proto of the CObj* (if CObj is a CObjObject, that is returned)
static inline CObjObject *cosmoO_grabProto(CObj *obj)
{
    return obj->type == COBJ_OBJECT ? (CObjObject *)obj : obj->proto;
}

void cosmoO_getRawObject(CState *state, CObjObject *proto, CValue key, CValue *val, CObj *obj);
void cosmoO_setRawObject(CState *state, CObjObject *proto, CValue key, CValue val, CObj *obj);
void cosmoO_indexObject(CState *state, CObjObject *object, CValue key, CValue *val);
void cosmoO_newIndexObject(CState *state, CObjObject *object, CValue key, CValue val);

// sets the user-defined pointer, if a user-define integer is already defined it will be over
// written
void cosmoO_setUserP(CObjObject *object, void *p);
// gets the user-defined pointer
void *cosmoO_getUserP(CObjObject *object);
// sets the user-defined integer, if a user-define pointer is already defined it will be over
// written
void cosmoO_setUserI(CObjObject *object, int i);
// gets the user-defined integer
int cosmoO_getUserI(CObjObject *object);
// sets the user-defined type
void cosmoO_setUserT(CObjObject *object, int t);
// gets the user type
int cosmoO_getUserT(CObjObject *object);
// locks the object
void cosmoO_lock(CObjObject *object);
// unlocks the object
void cosmoO_unlock(CObjObject *object);

// internal string
bool cosmoO_getIString(CState *state, CObjObject *object, int flag, CValue *val);

// copies the *str buffer to the heap and returns a CObjString struct which is also on the heap
// (length should not include the null terminator)
CObjString *cosmoO_copyString(CState *state, const char *str, size_t length);

// length shouldn't include the null terminator! str should be a null terminated string! (char array
// should also have been allocated using cosmoM_xmalloc!)
CObjString *cosmoO_takeString(CState *state, char *str, size_t length);

// allocates a CObjStruct pointing directly to *str
CObjString *cosmoO_allocateString(CState *state, const char *str, size_t length, uint32_t hash);

/*
    limited format strings to push onto the VM stack, formatting supported:

    '%d' - integers         [int]
    '%f' - floating point   [double]
    '%s' - strings          [char*]
        '%s' also accepts '%*s' which looks for a length specifier before the char* array
*/
CObjString *cosmoO_pushVFString(CState *state, const char *format, va_list args);

COSMO_API void printObject(CObj *o);
const char *cosmoO_typeStr(CObj *obj);

CObjString *cosmoO_toString(CState *state, CObj *obj);
cosmo_Number cosmoO_toNumber(CState *state, CObj *obj);
int cosmoO_count(CState *state, CObj *obj);

#endif
