#ifndef CTABLE_H
#define CTABLE_H

/* TODO: rewrite this table implementation. compared to other languages (including python!) this
 * table is verrryyyy slow */

#include "cosmo.h"
#include "cvalue.h"

typedef struct CTableEntry
{
    CValue key;
    CValue val;
} CTableEntry;

typedef struct CTable
{
    int count;
    int capacityMask; // +1 to get the capacity
    int tombstones;
    CTableEntry *table;
} CTable;

COSMO_API void cosmoT_initTable(CState *state, CTable *tbl, int startCap);
COSMO_API void cosmoT_clearTable(CState *state, CTable *tbl);
COSMO_API int cosmoT_count(CTable *tbl);

bool cosmoT_checkShrink(CState *state, CTable *tbl);

CObjString *cosmoT_lookupString(CTable *tbl, const char *str, int length, uint32_t hash);
CValue *cosmoT_insert(CState *state, CTable *tbl, CValue key);
bool cosmoT_get(CState *state, CTable *tbl, CValue key, CValue *val);
bool cosmoT_remove(CState *state, CTable *tbl, CValue key);

void cosmoT_printTable(CTable *tbl, const char *name);

#endif
