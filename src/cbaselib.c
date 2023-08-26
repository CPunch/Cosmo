#include "cbaselib.h"

#include "_time.h"
#include "cdebug.h"
#include "cmem.h"
#include "cobj.h"
#include "cvalue.h"
#include "cvm.h"

#include <math.h>

// ================================================================ [BASELIB]

int cosmoB_print(CState *state, int nargs, CValue *args)
{
    for (int i = 0; i < nargs; i++) {
        if (IS_REF(args[i])) { // if its a CObj*, generate the CObjString
            CObjString *str = cosmoV_toString(state, args[i]);
            printf("%s", cosmoO_readCString(str));
        } else { // else, thats pretty expensive for primitives, just print the raw value
            printValue(args[i]);
        }
    }
    printf("\n");

    return 0; // print doesn't return any args
}

int cosmoB_assert(CState *state, int nargs, CValue *args)
{
    if (nargs < 1 || nargs > 2) {
        cosmoV_error(state, "assert() expected 1 or 2 arguments, got %d!", nargs);
    }

    if (!IS_BOOLEAN(args[0]) || (nargs == 2 && !IS_STRING(args[1]))) {
        if (nargs == 2) {
            cosmoV_typeError(state, "assert()", "<boolean>, <string>", "%s, %s",
                             cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        } else {
            cosmoV_typeError(state, "assert()", "<boolean>", "%s", cosmoV_typeStr(args[0]));
        }
    }

    if (!cosmoV_readBoolean(args[0])) // expression passed was false, error!
        cosmoV_error(state, "%s", nargs == 2 ? cosmoV_readCString(args[1]) : "assert() failed!");

    return 0;
}

int cosmoB_type(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "type() expected 1 argument, got %d!", nargs);
    }

    // push the type string to the stack
    cosmoV_pushString(state, cosmoV_typeStr(args[0]));
    return 1; // 1 return value, the type string :D
}

int cosmoB_pcall(CState *state, int nargs, CValue *args)
{
    if (nargs < 1) {
        cosmoV_error(state, "pcall() expected at least 1 argument!");
    }

    // call the passed callable, the passed arguments are already in the
    // proper order lol, so we can just call it
    bool res = cosmoV_pcall(state, nargs - 1, 1);

    // insert false before the result
    cosmo_insert(state, 0, cosmoV_newBoolean(res));
    return 2;
}

int cosmoB_tonumber(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "tonumber() expected 1 argument, got %d!", nargs);
    }

    cosmoV_pushNumber(state, cosmoV_toNumber(state, args[0]));
    return 1;
}

int cosmoB_tostring(CState *state, int nargs, CValue *args)
{
    if (nargs != 1)
        cosmoV_error(state, "tostring() expected 1 argument, got %d!", nargs);

    cosmoV_pushRef(state, (CObj *)cosmoV_toString(state, args[0]));
    return 1;
}

int cosmoB_loadstring(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "loadstring() expected 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "loadstring()", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    CObjString *str = cosmoV_readString(args[0]);
    bool res = cosmoV_compileString(state, str->str, "");

    cosmo_insert(state, 0, cosmoV_newBoolean(res));
    return 2; // <boolean>, <closure> or <error>
}

int cosmoB_error(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "error() expected 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "error()", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_error(state, "%s", cosmoV_readCString(args[0]));

    return 0;
}

void cosmoB_loadLibrary(CState *state)
{
    const char *identifiers[] = {"print",    "assert",   "type",       "pcall",
                                 "tonumber", "tostring", "loadstring", "error"};

    CosmoCFunction baseLib[] = {cosmoB_print,    cosmoB_assert,   cosmoB_type,       cosmoB_pcall,
                                cosmoB_tonumber, cosmoB_tostring, cosmoB_loadstring, cosmoB_error};

    int i;
    for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, baseLib[i]);
    }

    // register all the pushed c functions and the strings as globals
    cosmoV_register(state, i);

    // load other libraries
    cosmoB_loadObjLib(state);
    cosmoB_loadStrLib(state);
    cosmoB_loadMathLib(state);
}

// ================================================================ [OBJECT.*]

int cosmoB_osetProto(CState *state, int nargs, CValue *args)
{
    if (nargs == 2) {
        CObj *obj = cosmoV_readRef(args[0]); // object to set proto too
        CObjObject *proto = cosmoV_readObject(args[1]);

        obj->proto = proto; // boom done
    } else {
        cosmoV_error(state, "Expected 2 arguments, got %d!", nargs);
    }

    return 0; // nothing
}

int cosmoB_ogetProto(CState *state, int nargs, CValue *args)
{
    if (nargs != 1)
        cosmoV_error(state, "Expected 1 argument, got %d!", nargs);

    cosmoV_pushRef(state, (CObj *)cosmoV_readObject(args[0])->_obj.proto); // just return the proto

    return 1; // 1 result
}

int cosmoB_oisChild(CState *state, int nargs, CValue *args)
{
    if (nargs != 2) {
        cosmoV_error(state, "object.ischild() expected 2 arguments, got %d!", nargs);
    }

    if (!IS_REF(args[0]) || !IS_OBJECT(args[1])) {
        cosmoV_typeError(state, "object.ischild()", "<reference obj>, <object>", "%s, %s",
                         cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
    }

    CObj *obj = cosmoV_readRef(args[0]);
    CObjObject *proto = cosmoV_readObject(args[1]);

    // push result
    cosmoV_pushBoolean(state, cosmoO_isDescendant(obj, proto));
    return 1;
}

COSMO_API void cosmoB_loadObjLib(CState *state)
{
    const char *identifiers[] = {"ischild"};

    CosmoCFunction objLib[] = {cosmoB_oisChild};

    // make object library object
    cosmoV_pushString(state, "object");

    // make __getter object for debug proto
    cosmoV_pushString(state, "__getter");

    // key & value pair
    cosmoV_pushString(state, "__proto");           // key
    cosmoV_pushCFunction(state, cosmoB_ogetProto); // value

    cosmoV_makeTable(state, 1);

    // make __setter table
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "__proto");
    cosmoV_pushCFunction(state, cosmoB_osetProto);

    cosmoV_makeTable(state, 1);

    int i;
    for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, objLib[i]);
    }

    // make the object and set the protoobject for all runtime-allocated objects
    CObjObject *obj = cosmoV_makeObject(state, i + 2); // + 2 for the getter/setter tables
    cosmoO_lock(obj); // lock so pesky people don't mess with it (feel free to remove if debugging)
    cosmoV_registerProtoObject(state, COBJ_OBJECT, obj);

    // register "object" to the global table
    cosmoV_register(state, 1);
}

// ================================================================ [OS.*]

// os.read()
int cosmoB_osRead(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "os.read() expected 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "os.read()", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    CObjString *str = cosmoV_readString(args[0]);

    // open file
    FILE *file = fopen(str->str, "rb");
    char *buf;
    size_t size, bRead;

    if (file == NULL) {
        // return nil, file doesn't exist
        return 0;
    }

    // grab the size of the file
    fseek(file, 0L, SEEK_END);
    size = ftell(file);
    rewind(file);

    buf = cosmoM_xmalloc(state, size + 1);        // +1 for the NULL terminator
    bRead = fread(buf, sizeof(char), size, file); // read the file into the buffer

    if (bRead < size) {
        // an error occured! we don't need to really throw an error, returning a nil is good enough
        return 0;
    }

    buf[bRead] = '\0'; // place the NULL terminator at the end of the buffer

    // push the string to the stack to return
    cosmoV_pushValue(state, cosmoV_newRef(cosmoO_takeString(state, buf, bRead)));
    return 1;
}

// os.time()
int cosmoB_osTime(CState *state, int nargs, CValue *args)
{
    struct timeval time;
    if (nargs > 0) {
        cosmoV_error(state, "os.time() expected no arguments, got %d!", nargs);
    }

    gettimeofday(&time, NULL);
    cosmoV_pushNumber(state, (time.tv_usec / 1000000.0) + time.tv_sec);
    return 1;
}

// os.system()
int cosmoB_osSystem(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "os.system() expects 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "os.system()", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    // run the command and return the exit code
    cosmoV_pushNumber(state, system(cosmoV_readCString(args[0])));
    return 1;
}

COSMO_API void cosmoB_loadOS(CState *state)
{
    const char *identifiers[] = {"read", "time", "system"};

    CosmoCFunction osLib[] = {cosmoB_osRead, cosmoB_osTime, cosmoB_osSystem};

    cosmoV_pushString(state, "os");

    int i;
    for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, osLib[i]);
    }

    cosmoV_makeObject(state, i);
    cosmoV_register(state, 1); // register the os.* object to the global table
}

// ================================================================ [STRING.*]

// string.sub
int cosmoB_sSub(CState *state, int nargs, CValue *args)
{
    if (nargs == 2) {
        if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
            cosmoV_typeError(state, "string.sub()", "<string>, <number>", "%s, %s",
                             cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        }

        CObjString *str = cosmoV_readString(args[0]);
        cosmo_Number indx = cosmoV_readNumber(args[1]);

        // make sure we stay within memory
        if (indx < 0 || indx >= str->length) {
            cosmoV_error(state, "string.sub() expected index to be 0-%d, got %d!", str->length - 1,
                         indx);
        }

        cosmoV_pushLString(state, str->str + ((int)indx), str->length - ((int)indx));
    } else if (nargs == 3) {
        if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
            cosmoV_typeError(state, "string.sub()", "<string>, <number>, <number>", "%s, %s, %s",
                             cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]),
                             cosmoV_typeStr(args[2]));
        }

        CObjString *str = cosmoV_readString(args[0]);
        cosmo_Number indx = cosmoV_readNumber(args[1]);
        cosmo_Number length = cosmoV_readNumber(args[2]);

        // make sure we stay within memory
        if (indx + length < 0 || indx + length >= str->length || indx < 0 || indx >= str->length) {
            cosmoV_error(
                state, "string.sub() expected subbed string goes out of bounds, max length is %d!",
                str->length);
        }

        cosmoV_pushLString(state, str->str + ((int)indx), ((int)length));
    } else {
        cosmoV_error(state, "string.sub() expected 2 or 3 arguments, got %d!", nargs);
    }

    return 1;
}

// string.find
int cosmoB_sFind(CState *state, int nargs, CValue *args)
{
    if (nargs == 2) {
        if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
            cosmoV_typeError(state, "string.find()", "<string>, <string>", "%s, %s",
                             cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
        }

        CObjString *str = cosmoV_readString(args[0]);
        CObjString *ptrn = cosmoV_readString(args[1]);

        char *indx = strstr(str->str, ptrn->str);

        // failed, return the error index -1
        if (indx == NULL) {
            cosmoV_pushNumber(state, -1);
            return 1;
        }

        // success! push the index
        cosmoV_pushNumber(state, (cosmo_Number)(indx - str->str));
    } else if (nargs == 3) {
        if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_NUMBER(args[2])) {
            cosmoV_typeError(state, "string.find()", "<string>, <string>, <number>", "%s, %s, %s",
                             cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]),
                             cosmoV_typeStr(args[2]));
        }

        CObjString *str = cosmoV_readString(args[0]);
        CObjString *ptrn = cosmoV_readString(args[1]);
        int startIndx = (int)cosmoV_readNumber(args[2]);

        char *indx = strstr(str->str + startIndx, ptrn->str);

        // failed, return the error index -1
        if (indx == NULL) {
            cosmoV_pushNumber(state, -1);
            return 1;
        }

        // success! push the index
        cosmoV_pushNumber(state, (cosmo_Number)(indx - str->str));
    } else {
        cosmoV_error(state, "string.find() expected 2 or 3 arguments, got %d!", nargs);
    }

    return 1;
}

// string.split
int cosmoB_sSplit(CState *state, int nargs, CValue *args)
{
    if (nargs != 2) {
        cosmoV_error(state, "string.split() expected 2 arguments, got %d!", nargs);
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        cosmoV_typeError(state, "string.split()", "<string>, <string>", "%s, %s",
                         cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
    }

    CObjString *str = cosmoV_readString(args[0]);
    CObjString *ptrn = cosmoV_readString(args[1]);

    int nEntries = 0;
    char *indx = str->str;
    char *nIndx;

    // while there are still patterns to match in the string, push the split strings onto the stack
    do {
        nIndx = strstr(indx, ptrn->str);

        cosmoV_pushNumber(state, nEntries++);
        cosmoV_pushLString(state, indx,
                           nIndx == NULL ? str->length - (indx - str->str) : nIndx - indx);

        indx = nIndx + ptrn->length;
    } while (nIndx != NULL);

    // finally, make a table out of the pushed entries
    cosmoV_makeTable(state, nEntries);
    return 1;
}

// string.byte
int cosmoB_sByte(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "string.byte() expected 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "string.byte", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    CObjString *str = cosmoV_readString(args[0]);

    if (str->length < 1) {
        // the length of the string is less than 1, in the future I might throw an error for this,
        // but for now im going to copy lua and just return a nil
        return 0;
    }

    // push the character byte and return
    cosmoV_pushNumber(state, (int)str->str[0]);
    return 1;
}

// string.char
int cosmoB_sChar(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "string.char() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "string.char", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    // small side effect of truncating the number, but ignoring the decimal instead of throwing an
    // error is the better option imo
    int num = (int)cosmoV_readNumber(args[0]);
    char c = num;

    if (num > 255 || num < 0) {
        cosmoV_error(state, "Character expected to be in range 0-255, got %d!", num);
    }

    // basically, treat the character value on the C stack as an """"array"""" with a length of 1
    cosmoV_pushLString(state, &c, 1);
    return 1;
}

int cosmoB_sLen(CState *state, int nargs, CValue *args)
{
    if (nargs < 1) {
        cosmoV_error(state, "string.len() expected 1 argument, got %d!", nargs);
    }

    if (!IS_STRING(args[0])) {
        cosmoV_typeError(state, "string.len", "<string>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, (cosmo_Number)strlen(cosmoV_readCString(args[0])));

    return 1;
}

int cosmoB_sRep(CState *state, int nargs, CValue *args)
{
    if (nargs != 2) {
        cosmoV_error(state, "string.rep() expected 2 arguments, got %d!", nargs);
    }

    // expects <string>, <number>
    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        cosmoV_typeError(state, "string.rep", "<string>, <number>", "%s, %s",
                         cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
    }

    CObjString *str = cosmoV_readString(args[0]);
    int times = (int)cosmoV_readNumber(args[1]);

    if (times <= 0) {
        cosmoV_error(state, "Expected times to be > 0, got %d!", times);
        return 0;
    }

    // allocated the new buffer for the string
    size_t length = str->length * times;
    char *newStr = cosmoM_xmalloc(state, length + 1); // + 1 for the NULL terminator

    // copy the string over the new buffer
    for (int i = 0; i < times; i++)
        memcpy(&newStr[i * str->length], str->str, str->length);

    // write the NULL terminator
    newStr[length] = '\0';

    // finally, push the resulting string onto the stack
    cosmoV_pushRef(state, (CObj *)cosmoO_takeString(state, newStr, length));
    return 1;
}

void cosmoB_loadStrLib(CState *state)
{
    const char *identifiers[] = {"sub", "find", "split", "byte", "char", "len", "rep"};

    CosmoCFunction strLib[] = {cosmoB_sSub,  cosmoB_sFind, cosmoB_sSplit, cosmoB_sByte,
                               cosmoB_sChar, cosmoB_sLen,  cosmoB_sRep};

    // make string library object
    cosmoV_pushString(state, "string");
    int i;
    for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, strLib[i]);
    }

    // make the object and set the protoobject for all strings
    CObjObject *obj = cosmoV_makeObject(state, i);
    cosmoO_lock(obj); // lock so pesky people don't mess with it (feel free to remove if debugging)
    cosmoV_registerProtoObject(state, COBJ_STRING, obj);

    // register "string" to the global table
    cosmoV_register(state, 1);
}

// ================================================================ [MATH]

// math.abs
int cosmoB_mAbs(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.abs() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.abs", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, fabs(cosmoV_readNumber(args[0])));
    return 1;
}

// math.floor
int cosmoB_mFloor(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.floor() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.floor", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, (int)cosmoV_readNumber(args[0]));
    return 1;
}

// math.ceil
int cosmoB_mCeil(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.ceil() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.ceil", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    int roundedDown = (int)cosmoV_readNumber(args[0]);

    // number is already truncated
    if ((double)roundedDown == cosmoV_readNumber(args[0])) {
        cosmoV_pushValue(state, args[0]);
    } else {
        cosmoV_pushNumber(state, roundedDown + 1);
    }

    return 1;
}

int cosmoB_mSin(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.sin() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.sin", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, sin(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mCos(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.cos() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.cos", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, cos(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mTan(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.tan() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.tan", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, tan(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mASin(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.asin() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.asin", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, asin(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mACos(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.acos() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.acos", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, acos(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mATan(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.atan() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.atan", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    cosmoV_pushNumber(state, atan(cosmoV_readNumber(args[0])));
    return 1;
}

int cosmoB_mRad(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.rad() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.rad", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    // convert the degree to radians
    cosmoV_pushNumber(state, cosmoV_readNumber(args[0]) * (acos(-1) / 180));
    return 1;
}

int cosmoB_mDeg(CState *state, int nargs, CValue *args)
{
    if (nargs != 1) {
        cosmoV_error(state, "math.deg() expected 1 argument, got %d!", nargs);
    }

    if (!IS_NUMBER(args[0])) {
        cosmoV_typeError(state, "math.deg", "<number>", "%s", cosmoV_typeStr(args[0]));
    }

    // convert the degree to radians
    cosmoV_pushNumber(state, cosmoV_readNumber(args[0]) * (180 / acos(-1)));
    return 1;
}

void cosmoB_loadMathLib(CState *state)
{
    const char *identifiers[] = {"abs",  "floor", "ceil", "sin", "cos", "tan",
                                 "asin", "acos",  "atan", "rad", "deg"};

    CosmoCFunction mathLib[] = {cosmoB_mAbs,  cosmoB_mFloor, cosmoB_mCeil, cosmoB_mSin,
                                cosmoB_mCos,  cosmoB_mTan,   cosmoB_mASin, cosmoB_mACos,
                                cosmoB_mATan, cosmoB_mRad,   cosmoB_mDeg};
    int i;

    // make math library object
    cosmoV_pushString(state, "math");
    for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
        cosmoV_pushString(state, identifiers[i]);
        cosmoV_pushCFunction(state, mathLib[i]);
    }

    cosmoV_pushString(state, "pi");
    cosmoV_pushNumber(state, acos(-1));
    i++;

    // make the object and register it as a global to the state
    cosmoV_makeObject(state, i);
    cosmoV_register(state, 1);
}

// ================================================================ [VM.*]

// vm.__getter["globals"]
int cosmoB_vgetGlobal(CState *state, int nargs, CValue *args)
{
    // this function doesn't need to check anything, just return the global table
    cosmoV_pushRef(state, (CObj *)state->globals);
    return 1;
}

// vm.__setter["globals"]
int cosmoB_vsetGlobal(CState *state, int nargs, CValue *args)
{
    if (nargs != 2) {
        cosmoV_error(state, "Expected 2 argumenst, got %d!", nargs);
    }

    if (!IS_TABLE(args[1])) {
        cosmoV_typeError(state, "vm.__setter[\"globals\"]", "<object>, <table>", "%s, %s",
                         cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
    }

    // this makes me very nervous ngl
    CObjTable *tbl = (CObjTable *)cosmoV_readRef(args[1]);
    state->globals = tbl;
    return 0;
}

// vm.disassemble()
int cosmoB_vdisassemble(CState *state, int nargs, CValue *args)
{
    CObjClosure *closure;

    if (nargs != 1) {
        cosmoV_error(state, "Expected 1 argument, got %d!", nargs);
    }

    // get the closure
    if (!IS_CLOSURE(args[0])) {
        cosmoV_typeError(state, "vm.disassemble", "<closure>", "%s", cosmoV_typeStr(args[0]));
    }

    closure = cosmoV_readClosure(args[0]);

    // print the disasembly
    disasmChunk(&closure->function->chunk,
                closure->function->name ? closure->function->name->str : UNNAMEDCHUNK, 0);
    return 0;
}

int cosmoB_vindexBProto(CState *state, int nargs, CValue *args)
{
    if (nargs != 2) {
        cosmoV_error(state, "Expected 2 arguments, got %d!", nargs);
    }

    if (!IS_NUMBER(args[1])) {
        cosmoV_typeError(state, "baseProtos.__index", "<object>, <number>", "%s, %s",
                         cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]));
    }

    int indx = (int)cosmoV_readNumber(args[1]);

    if (indx >= COBJ_MAX || indx < 0) {
        cosmoV_error(state, "index out of range! expected 0 - %d, got %d!", COBJ_MAX - 1, indx);
        return 0;
    }

    if (state->protoObjects[indx] != NULL)
        cosmoV_pushRef(state, (CObj *)state->protoObjects[indx]);
    else
        cosmoV_pushNil(state);

    return 1; // 1 value pushed, 1 value returned
}

int cosmoB_vnewindexBProto(CState *state, int nargs, CValue *args)
{
    if (nargs != 3) {
        cosmoV_error(state, "Expected 3 arguments, got %d!", nargs);
    }

    if (!IS_NUMBER(args[1]) || !IS_OBJECT(args[2])) {
        cosmoV_typeError(state, "baseProtos.__newindex", "<object>, <number>, <object>",
                         "%s, %s, %s", cosmoV_typeStr(args[0]), cosmoV_typeStr(args[1]),
                         cosmoV_typeStr(args[2]));
    }

    int indx = (int)cosmoV_readNumber(args[1]);
    CObjObject *proto = cosmoV_readObject(args[2]);

    if (indx >= COBJ_MAX || indx < 0) {
        cosmoV_error(state, "index out of range! expected 0 - %d, got %d!", COBJ_MAX, indx);
    }

    cosmoV_registerProtoObject(state, indx, proto);
    return 0; // we don't return anything
}

// vm.collect()
int cosmoB_vcollect(CState *state, int nargs, CValue *args)
{
<<<<<<< HEAD
    // now force a garbage collection
    cosmoM_collectGarbage(state);

=======
    // force a garbage collection
    cosmoM_collectGarbage(state);
>>>>>>> 409937c (fix vm.collect())
    return 0;
}

void cosmoB_loadVM(CState *state)
{
    // make vm.* object
    cosmoV_pushString(state, "vm");

    // make vm.baseProtos object
    cosmoV_pushString(state, "baseProtos");

    cosmoV_pushString(state, "__index");
    cosmoV_pushCFunction(state, cosmoB_vindexBProto);

    cosmoV_pushString(state, "__newindex");
    cosmoV_pushCFunction(state, cosmoB_vnewindexBProto);

    cosmoV_makeObject(state, 2); // makes the baseProtos object

    // make __getter table for vm object
    cosmoV_pushString(state, "__getter");

    cosmoV_pushString(state, "globals");
    cosmoV_pushCFunction(state, cosmoB_vgetGlobal);

    cosmoV_makeTable(state, 1);

    // make __setter table for vm object
    cosmoV_pushString(state, "__setter");

    cosmoV_pushString(state, "globals");
    cosmoV_pushCFunction(state, cosmoB_vsetGlobal);

    cosmoV_makeTable(state, 1);

    cosmoV_pushString(state, "collect");
    cosmoV_pushCFunction(state, cosmoB_vcollect);

    cosmoV_pushString(state, "disassemble");
    cosmoV_pushCFunction(state, cosmoB_vdisassemble);

    cosmoV_makeObject(state, 5); // makes the vm object

    // register "vm" to the global table
    cosmoV_register(state, 1);
}
