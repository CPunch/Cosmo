#include "cdump.h"

#include "cdebug.h"
#include "cmem.h"
#include "cobj.h"
#include "cvalue.h"
#include "cvm.h"

typedef struct
{
    CState *state;
    const void *userData;
    cosmo_Writer writer;
    int writerStatus;
} DumpState;

static bool writeCValue(DumpState *dstate, CValue val);

#define check(e)                                                                                   \
    if (!e) {                                                                                      \
        return false;                                                                              \
    }

static void initDumpState(CState *state, DumpState *dstate, cosmo_Writer writer,
                          const void *userData)
{
    dstate->state = state;
    dstate->userData = userData;
    dstate->writer = writer;
    dstate->writerStatus = 0;
}

static bool writeBlock(DumpState *dstate, const void *data, size_t size)
{
    if (dstate->writerStatus == 0) {
        dstate->writerStatus = dstate->writer(dstate->state, data, size, dstate->userData);
    }

    return dstate->writerStatus == 0;
}

static bool writeu8(DumpState *dstate, uint8_t d)
{
    return writeBlock(dstate, &d, sizeof(uint8_t));
}

static bool writeu32(DumpState *dstate, uint32_t d)
{
    return writeBlock(dstate, &d, sizeof(uint32_t));
}

static bool writeSize(DumpState *dstate, size_t d)
{
    return writeBlock(dstate, &d, sizeof(size_t));
}

static bool writeVector(DumpState *dstate, const void *data, size_t size, size_t count)
{
    check(writeSize(dstate, count));
    check(writeBlock(dstate, data, size * count));

    return true;
}

static bool writeHeader(DumpState *dstate)
{
    check(writeBlock(dstate, COSMO_MAGIC, COSMO_MAGIC_LEN));

    /* after the magic, we write some platform information */
    check(writeu8(dstate, cosmoD_isBigEndian()));
    check(writeu8(dstate, sizeof(cosmo_Number)));
    check(writeu8(dstate, sizeof(size_t)));
    check(writeu8(dstate, sizeof(int)));

    return true;
}

static bool writeCObjString(DumpState *dstate, CObjString *obj)
{
    if (obj == NULL) { /* this is in case cobjfunction's name or module strings are null */
        check(writeu32(dstate, 0));
        return true;
    }

    /* write string length */
    check(writeu32(dstate, obj->length));

    /* write string data */
    check(writeBlock(dstate, obj->str, obj->length));

    return true;
}

static bool writeCObjFunction(DumpState *dstate, CObjFunction *obj)
{
    check(writeCObjString(dstate, obj->name));
    check(writeCObjString(dstate, obj->module));

    check(writeu32(dstate, obj->args));
    check(writeu32(dstate, obj->upvals));
    check(writeu8(dstate, obj->variadic));

    /* write chunk info */
    check(writeVector(dstate, obj->chunk.buf, sizeof(uint8_t), obj->chunk.count));

    /* write line info */
    check(writeVector(dstate, obj->chunk.lineInfo, sizeof(int), obj->chunk.count));

    /* write constants */
    check(writeSize(dstate, obj->chunk.constants.count));
    for (int i = 0; i < obj->chunk.constants.count; i++) {
        check(writeCValue(dstate, obj->chunk.constants.values[i]));
    }

    return true;
}

static bool writeCObj(DumpState *dstate, CObj *obj)
{
    /*
        we can kind of cheat here since our parser only emits a few very limited CObjs...
        CChunks will only ever have the following CObj's in their constant table:
        - COBJ_STRING
        - COBJ_FUNCTION

        the rest of the objects are created during runtime. yay!
    */
    CObjType t = cosmoO_readType(obj);

    /* write cobj type */
    writeu8(dstate, t);

    /* write object payload/body */
    switch (t) {
    case COBJ_STRING:
        check(writeCObjString(dstate, (CObjString *)obj));
        break;
    case COBJ_FUNCTION:
        check(writeCObjFunction(dstate, (CObjFunction *)obj));
        break;
    default:
        cosmoV_error(dstate->state, "invalid cobj type: %d", t);
        return false;
    }

    return true;
}

#define WRITE_VAR(dstate, type, expression)                                                        \
    {                                                                                              \
        type _tmp = expression;                                                                    \
        check(writeBlock(dstate, &_tmp, sizeof(_tmp)));                                            \
        break;                                                                                     \
    }

static bool writeCValue(DumpState *dstate, CValue val)
{
    CosmoType t = GET_TYPE(val);

    /* write value type */
    writeu8(dstate, t);

    /* write value payload/body */
    switch (t) {
    case COSMO_TNUMBER:
        WRITE_VAR(dstate, cosmo_Number, cosmoV_readNumber(val))
    case COSMO_TBOOLEAN:
        WRITE_VAR(dstate, bool, cosmoV_readBoolean(val))
    case COSMO_TREF:
        check(writeCObj(dstate, cosmoV_readRef(val)));
        break;
    case COSMO_TNIL: /* no body */
        break;
    default:
        cosmoV_error(dstate->state, "invalid value type: %d", t);
        return false;
    }

    return true;
}

#undef WRITE_VAR

bool cosmoD_isBigEndian()
{
    union
    {
        uint32_t i;
        uint8_t c[4];
    } _indxint = {0xDEADB33F};

    return _indxint.c[0] == 0xDE;
}

int cosmoD_dump(CState *state, CObjFunction *func, cosmo_Writer writer, const void *userData)
{
    DumpState dstate;
    initDumpState(state, &dstate, writer, userData);

    check(writeHeader(&dstate));
    check(writeCObjFunction(&dstate, func));

    return dstate.writerStatus;
}
