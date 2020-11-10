#ifndef COSMO_BASELIB
#define COSMO_BASELIB

#include "cstate.h"

COSMO_API void cosmoB_loadlibrary(CState *state);
COSMO_API CValue cosmoB_print(CState *state, int nargs, CValue *args);

#endif