#ifndef COSMO_BASELIB
#define COSMO_BASELIB

#include "cstate.h"


COSMO_API void cosmoB_loadLibrary(CState *state);
COSMO_API void cosmoB_loadDebug(CState *state);
COSMO_API int cosmoB_print(CState *state, int nargs, CValue *args);
COSMO_API int cosmoB_assert(CState *state, int nargs, CValue *args);

#endif