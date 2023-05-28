#include "cundump.h"

#include "cchunk.h"
#include "cdump.h"
#include "cmem.h"
#include "cvm.h"

typedef struct
{
    CState *state;
    const void *userData;
    cosmo_Reader reader;
    int readerStatus;
} UndumpState;

static bool readCValue(UndumpState *udstate, CValue *val);

#define check(e)                                                                                   \
    if (!e) {                                                                                      \
        printf("FAILED %d\n", __LINE__);                                                           \
        return false;                                                                              \
    }

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

static bool readu32(UndumpState *udstate, uint32_t *d)
{
    return readBlock(udstate, d, sizeof(uint32_t));
}

static bool readSize(UndumpState *udstate, size_t *d)
{
    return readBlock(udstate, d, sizeof(size_t));
}

static bool readVector(UndumpState *udstate, void **data, size_t size, size_t *count)
{
    check(readSize(udstate, count));
    *data = cosmoM_xmalloc(udstate->state, (*count) * size);
    return readBlock(udstate, *data, (*count) * size);
}

#define checku8(udstate, d, tmp)                                                                   \
    check(readu8(udstate, &tmp));                                                                  \
    if (d != tmp) {                                                                                \
        cosmoV_error(udstate->state, "bad header!");                                               \
        return false;                                                                              \
    }

static bool checkHeader(UndumpState *udstate)
{
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
    uint32_t size;
    char *data;

    check(readu32(udstate, (uint32_t *)&size));
    if (size == 0) { /* empty string */
        *str = NULL;
        return true;
    }

    data = cosmoM_xmalloc(udstate->state, size + 1);
    check(readBlock(udstate, (void *)data, size));
    data[size] = '\0'; /* add NULL-terminator */

    *str = cosmoO_takeString(udstate->state, data, size);
    return true;
}

static bool readCObjFunction(UndumpState *udstate, CObjFunction **func)
{
    size_t constants;
    CValue val;

    *func = cosmoO_newFunction(udstate->state);

    /* make sure our GC can see that we're currently using this function (and the values it uses) */
    cosmoV_pushRef(udstate->state, (CObj *)*func);

    check(readCObjString(udstate, &(*func)->name));
    check(readCObjString(udstate, &(*func)->module));

    check(readu32(udstate, (uint32_t *)&(*func)->args));
    check(readu32(udstate, (uint32_t *)&(*func)->upvals));
    check(readu8(udstate, (uint8_t *)&(*func)->variadic));

    /* read chunk info */
    check(
        readVector(udstate, (void **)&(*func)->chunk.buf, sizeof(uint8_t), &(*func)->chunk.count));
    check(
        readVector(udstate, (void **)&(*func)->chunk.lineInfo, sizeof(int), &(*func)->chunk.count));

    /* read constants */
    check(readSize(udstate, &constants));
    for (int i = 0; i < constants; i++) {
        check(readCValue(udstate, &val));
        addConstant(udstate->state, &(*func)->chunk, val);
    }

    /* pop function off stack */
    cosmoV_pop(udstate->state);
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

#define READ_VAR(udstate, val, type, creator)                                                      \
    {                                                                                              \
        type _tmp;                                                                                 \
        check(readBlock(udstate, &_tmp, sizeof(type)));                                            \
        *val = creator(_tmp);                                                                      \
        break;                                                                                     \
    }

static bool readCValue(UndumpState *udstate, CValue *val)
{
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

int cosmoD_undump(CState *state, cosmo_Reader reader, const void *userData, CObjFunction **func)
{
    UndumpState udstate;
    initUndumpState(state, &udstate, reader, userData);

    if (!checkHeader(&udstate)) {
        cosmoV_pushNil(state);
        return 1;
    }

    if (!readCObjFunction(&udstate, func)) {
        cosmoV_pushNil(state);
        return 1;
    }

    cosmoV_pushRef(state, (CObj *)*func);
    return udstate.readerStatus;
}