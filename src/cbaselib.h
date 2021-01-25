#ifndef COSMO_BASELIB
#define COSMO_BASELIB

#include "cstate.h"

/* loads all of the base library, including:
    - base library ("print", "assert", "type", "pcall", "loadstring", etc.)
    - string library ("string.sub", "string.find", "string.split", "string.charAt")
*/
COSMO_API void cosmoB_loadLibrary(CState *state);

/* loads the base string library, including:
    - string.sub
    - stirng.find
    - string.split
    - string.charAt

    The base proto object for strings is also set, allowing you to invoke the string.* api through string objects, eg.
        `"hello world":split(" ")` is equivalent to `string.split("hello world", " ")`
*/
COSMO_API void cosmoB_loadStrLib(CState *state);

/* sets the base proto of all objects to the debug proto which allows for
    - manipulation of the ProtoObject of objects through the '__proto' field

    additionally, the vm.* library is loaded, including:
    - manually setting/grabbing base protos of any object (vm.baseProtos)

    for this reason, it is recommended to NOT load this library in production
*/
COSMO_API void cosmoB_loadDebug(CState *state);

#define cosmoV_typeError(state, name, expectedTypes, formatStr, ...) \
        cosmoV_error(state, name " expected (" expectedTypes "), got (" formatStr ")!", __VA_ARGS__);

#endif
