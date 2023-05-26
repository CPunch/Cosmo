#include "cdump.h"
#include "cundump.h"
#include "cvm.h"
#include "cchunk.h"

typedef struct
{
    CState *state;
    const void *userData;
    cosmo_Reader reader;
    int readerStatus;
} UndumpState;

#define check(e) if (!e) return false;

static void initUndumpState(CState *state, UndumpState *udstate, cosmo_Reader reader,
                            const void *userData)
{
    udstate->state = state;
    udstate->userData = userData;
    udstate->reader = reader;
    udstate->readerStatus = 0;
}

static bool readBlock(UndumpState *udstate, void *data, size_t size)
{
    if (udstate->readerStatus == 0) {
        udstate->readerStatus = udstate->reader(udstate->state, data, size, udstate->userData);
    }

    return udstate->readerStatus == 0;
}

static bool readu8(UndumpState *udstate, uint8_t *d)
{
    return readBlock(udstate, d, sizeof(uint8_t));
}

static bool readu16(UndumpState *udstate, uint16_t *d)
{
    return readBlock(udstate, d, sizeof(uint16_t));
}

static bool readSize(UndumpState *udstate, size_t *d)
{
    return readBlock(udstate, d, sizeof(size_t));
}

static bool readVector(UndumpState *udstate, void **data, size_t *size)
{
    check(readSize(udstate, size));
    *data = cosmoM_malloc(udstate->state, *size);
    return readBlock(udstate, *data, *size);
}

#define checku8(udstate, d, tmp) \
    check(readu8(udstate, &tmp)); \
    if (d != tmp) { \
        cosmoV_error(udstate->state, "bad header!"); \
        return false; \
    }

static bool checkHeader(UndumpState *udstate) {
    char magic[COSMO_MAGIC_LEN];
    uint8_t tmp;

    /* check header */
    readBlock(udstate, magic, COSMO_MAGIC_LEN);
    if (memcmp(magic, COSMO_MAGIC, COSMO_MAGIC_LEN) != 0) {
        cosmoV_error(udstate->state, "bad header!");
        return false;
    }

    /* after the magic, we read some platform information */
    checku8(udstate, cosmoD_isBigEndian(), tmp);
    checku8(udstate, sizeof(cosmo_Number), tmp);
    checku8(udstate, sizeof(size_t), tmp);
    checku8(udstate, sizeof(int), tmp);

    return true;
}

#undef checku8

static bool readCObjString(UndumpState *udstate, CObjString **str)
{
    size_t size;
    char *data;

    check(readu32(udstate, &size));
    if (size == 0) { /* empty string */
        *str = NULL;
        return true;
    }

    *data = cosmoM_malloc(udstate->state, size+1);
    check(readBlock(udstate, (void *)&data, size));
    data[size] = '\0'; /* add NULL-terminator */

    *str = cosmoO_takeString(udstate->state, data, size);
    return true;
}

static bool readCObjFunction(UndumpState *udstate, CObjFunction **func) {
    *func = cosmoO_newFunction(udstate->state);

    check(readCObjString(udstate, &(*func)->name));
    check(readCObjString(udstate, &(*func)->module));

    check(readu32(udstate, &(*func)->args));
    check(readu32(udstate, &(*func)->upvals));
    check(readu8(udstate, &(*func)->variadic));

    /* read chunk info */
    check(readVector(udstate, (void **)&(*func)->chunk.buf, &(*func)->chunk.count));
    check(readVector(udstate, (void **)&(*func)->chunk.lineInfo, &(*func)->chunk.count));

    /* read constants */
    size_t constants;
    check(readSize(udstate, &constants));
    for (int i = 0; i < constants; i++) {
        CValue val;
        check(readCValue(udstate, &val));
        addConstant(udstate->state, &(*func)->chunk, val);
    }

    return true;
}

static bool readCObj(UndumpState *udstate, CObj **obj)
{
    uint8_t type;
    check(readu8(udstate, &type));

    switch (type) {
    case COBJ_STRING:
        return readCObjString(udstate, (CObjString **)obj);
    case COBJ_FUNCTION:
        return readCObjFunction(udstate, (CObjFunction **)obj);
    default:
        cosmoV_error(udstate->state, "unknown object type!");
        return false;
    }

    return true;
}

#define READ_VAR(udstate, val, type, creator) \
    { \
        type _tmp; \
        check(readBlock(udstate, &_tmp, sizeof(type))); \
        *val = creator(_tmp); \
        break; \
    }

static bool readCValue(UndumpState *udstate, CValue *val) {
    uint8_t type;
    check(readu8(udstate, &type));

    switch (type) {
    case COSMO_TNUMBER:
        READ_VAR(udstate, val, cosmo_Number, cosmoV_newNumber)
    case COSMO_TBOOLEAN:
        READ_VAR(udstate, val, bool, cosmoV_newBoolean)
    case COSMO_TREF: {
        CObj *obj;
        check(readCObj(udstate, (CObj **)&obj));
        *val = cosmoV_newRef(obj);
        break;
    }
    case COSMO_TNIL:
        *val = cosmoV_newNil();
        break;
    default:
        break;
    }

    return true;
}

int cosmoD_undump(CState *state, CObjFunction *func, cosmo_Reader writer, const void *userData) {
    UndumpState udstate;
    initUndumpState(state, &udstate, writer, userData);

    if (!checkHeader(&udstate)) {
        return 1;
    }

    if (!readCObjFunction(&udstate, &func)) {
        return 1;
    }

    return udstate.readerStatus;
}