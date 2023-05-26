#ifndef COSMO_DUMP_H
#define COSMO_DUMP_H

#include "cobj.h"
#include "cosmo.h"

#include <stdio.h>

#define COSMO_MAGIC     "COS\x12"
#define COSMO_MAGIC_LEN 4

typedef int (*cosmo_Writer)(CState *state, const void *data, size_t size, const void *ud);

bool cosmoD_isBigEndian();

int cosmoD_dump(CState *state, CObjFunction *func, cosmo_Writer writer, const void *userData);

#endif