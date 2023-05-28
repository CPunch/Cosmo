#include "cdump.h"

#include "cdebug.h"
#include "cmem.h"
#include "cobj.h"
#include "cvalue.h"

typedef struct
{
    CState *state;
    const void *userData;
    cosmo_Writer writer;
    int writerStatus;
} DumpState;

static void writeCValue(DumpState *dstate, CValue val);

static void initDumpState(CState *state, DumpState *dstate, cosmo_Writer writer,
                          const void *userData)
{
    dstate->state = state;
    dstate->userData = userData;
    dstate->writer = writer;
    dstate->writerStatus = 0;
}

static void writeBlock(DumpState *dstate, const void *data, size_t size)
{
    if (dstate->writerStatus == 0) {
        dstate->writerStatus = dstate->writer(dstate->state, data, size, dstate->userData);
    }
}

static void writeu8(DumpState *dstate, uint8_t d)
{
    writeBlock(dstate, &d, sizeof(uint8_t));
}

static void writeu32(DumpState *dstate, uint32_t d)
{
    writeBlock(dstate, &d, sizeof(uint32_t));
}

static void writeSize(DumpState *dstate, size_t d)
{
    writeBlock(dstate, &d, sizeof(size_t));
}

static void writeVector(DumpState *dstate, const void *data, size_t size, size_t count)
{
    writeSize(dstate, count);
    writeBlock(dstate, data, size * count);
}

static void writeHeader(DumpState *dstate)
{
    writeBlock(dstate, COSMO_MAGIC, COSMO_MAGIC_LEN);

    /* after the magic, we write some platform information */
    writeu8(dstate, cosmoD_isBigEndian());
    writeu8(dstate, sizeof(cosmo_Number));
    writeu8(dstate, sizeof(size_t));
    writeu8(dstate, sizeof(int));
}

static void writeCObjString(DumpState *dstate, CObjString *obj)
{
    if (obj == NULL) { /* this is in case cobjfunction's name or module strings are null */
        writeu32(dstate, 0);
        return;
    }

    /* write string length */
    writeu32(dstate, obj->length);

    /* write string data */
    writeBlock(dstate, obj->str, obj->length);
}

static void writeCObjFunction(DumpState *dstate, CObjFunction *obj)
{
    writeCObjString(dstate, obj->name);
    writeCObjString(dstate, obj->module);

    writeu32(dstate, obj->args);
    writeu32(dstate, obj->upvals);
    writeu8(dstate, obj->variadic);

    /* write chunk info */
    writeVector(dstate, obj->chunk.buf, sizeof(uint8_t), obj->chunk.count);

    /* write line info */
    writeVector(dstate, obj->chunk.lineInfo, sizeof(int), obj->chunk.count);

    /* write constants */
    writeSize(dstate, obj->chunk.constants.count);
    for (int i = 0; i < obj->chunk.constants.count; i++) {
        writeCValue(dstate, obj->chunk.constants.values[i]);
    }
}

static void writeCObj(DumpState *dstate, CObj *obj)
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
        writeCObjString(dstate, (CObjString *)obj);
        break;
    case COBJ_FUNCTION:
        writeCObjFunction(dstate, (CObjFunction *)obj);
        break;
    default:
        break;
    }
}

#define WRITE_VAR(dstate, type, expression)                                                        \
    {                                                                                              \
        type _tmp = expression;                                                                    \
        writeBlock(dstate, &_tmp, sizeof(_tmp));                                                   \
        break;                                                                                     \
    }

static void writeCValue(DumpState *dstate, CValue val)
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
        writeCObj(dstate, cosmoV_readRef(val));
        break;
    case COSMO_TNIL: /* fallthrough, no body */
    default:
        break;
    }
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

    writeHeader(&dstate);
    writeCObjFunction(&dstate, func);

    return dstate.writerStatus;
}
