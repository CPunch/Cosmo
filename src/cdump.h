#ifndef COSMO_DUMP_H
#define COSMO_DUMP_H

#include "cobj.h"
#include "cosmo.h"

#include <stdio.h>

#define COSMO_MAGIC     "COS\x12"
#define COSMO_MAGIC_LEN 4

bool cosmoD_isBigEndian();

/* returns non-zero on error */
int cosmoD_dump(CState *state, CObjFunction *func, cosmo_Writer writer, const void *userData);

#endif