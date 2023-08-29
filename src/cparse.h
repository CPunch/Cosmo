#ifndef CPARSE_H
#define CPARSE_H

#include "clex.h"
#include "cosmo.h"

// compiles source into CChunk
CObjFunction *cosmoP_compileString(CState *state, const char *source, const char *module);

#endif
