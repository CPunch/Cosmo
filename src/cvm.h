#ifndef COSMOVM_H
#define COSMOVM_H

#include "cosmo.h"
#include "cstate.h"

typedef enum {
    COSMOVM_OK,
    COSMOVM_RUNTIME_ERR,
    COSMOVM_BUILDTIME_ERR
} COSMOVMRESULT;

// args = # of pass parameters, nresults = # of expected results
COSMO_API COSMOVMRESULT cosmoV_call(CState *state, int args, int nresults);

#endif