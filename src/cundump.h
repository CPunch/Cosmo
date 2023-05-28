#ifndef COSMO_UNDUMP_H
#define COSMO_UNDUMP_H

#include "cobj.h"
#include "cosmo.h"

#include <stdio.h>

/* returns non-zero on error */
int cosmoD_undump(CState *state, cosmo_Reader reader, const void *userData, CObjFunction **func);

#endif