#ifndef COSMO_UNDUMP_H
#define COSMO_UNDUMP_H

#include "cobj.h"
#include "cosmo.h"

#include <stdio.h>

typedef int (*cosmo_Reader)(CState *state, void *data, size_t size, const void *ud);

/* returns non-zero on error */
int cosmoD_undump(CState *state, CObjFunction *func, cosmo_Reader writer, const void *userData);

#endif