#ifndef COSMO_BASELIB
#define COSMO_BASELIB

#include "cstate.h"

/* loads all of the base library, including:
    - base library ("print", "assert", "type", "pcall", "loadstring", etc.)
    - string library ("string.sub", "string.split", "string.find")
*/
COSMO_API void cosmoB_loadLibrary(CState *state);
COSMO_API void cosmoB_loadStrLib(CState *state);
COSMO_API void cosmoB_loadDebug(CState *state);
COSMO_API int cosmoB_print(CState *state, int nargs, CValue *args);
COSMO_API int cosmoB_assert(CState *state, int nargs, CValue *args);
COSMO_API int cosmoB_type(CState *state, int nargs, CValue *args);
COSMO_API int cosmoB_pcall(CState *state, int nargs, CValue *args);

#define cosmoV_typeError(state, name, expectedTypes, formatStr, ...) \
        cosmoV_error(state, name " expected (" expectedTypes "), got (" formatStr ")!", __VA_ARGS__);

#endif
