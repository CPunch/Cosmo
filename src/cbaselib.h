#ifndef COSMO_BASELIB
#define COSMO_BASELIB

#include "cstate.h"

/* loads all of the base library, including:
    - base library ("print", "assert", "type", "pcall", "loadstring", etc.)
    - object library
    - string library
    - math library
*/
COSMO_API void cosmoB_loadLibrary(CState *state);

/* loads the base object library, including:
    - object.ischild or <obj>:ischild()
    - object.__proto (allows grabbing and setting proto objects)
*/
COSMO_API void cosmoB_loadObjLib(CState *state);

/* loads the os library, including:
    - os.read()
    - os.system()
    - os.time()
*/
COSMO_API void cosmoB_loadOS(CState *state);

/* loads the base string library, including:
    - string.sub & <string>:sub()
    - stirng.find & <string>:find()
    - string.split & <string>:split()
    - string.byte & <string>:byte()
    - string.char & <string>:char()
    - string.rep & <string>:rep()

    The base proto object for strings is also set, allowing you to invoke the string.* api through
   string objects, eg.
        `"hello world":split(" ")` is equivalent to `string.split("hello world", " ")`
*/
COSMO_API void cosmoB_loadStrLib(CState *state);

/* loads the base math library, including:
    - math.abs
    - math.floor
    - math.ceil
    - math.tan
*/
COSMO_API void cosmoB_loadMathLib(CState *state);

/* loads the vm library, including:
    - manually setting/grabbing base protos of any object (vm.baseProtos)
    - manually setting/grabbing the global table (vm.globals)
    - manually invoking a garbage collection event (vm.collect())
    - printing closure disassemblies (vm.disassemble())

    for this reason, it is recommended to NOT load this library in production
*/
COSMO_API void cosmoB_loadVM(CState *state);

#define cosmoV_typeError(state, name, expectedTypes, formatStr, ...)                               \
    cosmoV_error(state, name " expected (" expectedTypes "), got (" formatStr ")!", __VA_ARGS__);

#endif
