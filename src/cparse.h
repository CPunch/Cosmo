#ifndef CPARSE_H
#define CPARSE_H

#include "cosmo.h"
#include "clex.h"

// compiles source into CChunk, if NULL is returned, a syntaxical error has occured and pushed onto the stack
CObjFunction* cosmoP_compileString(CState *state, const char *source);

#endif