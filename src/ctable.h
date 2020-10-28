#ifndef CTABLE_H
#define CTABLE_H

#include "cosmo.h"
#include "cvalue.h"

typedef struct CTableEntry {
    CValue key;
    CValue val;
} CTableEntry;

typedef struct CTable {
    int count;
    int capacity;
    CTableEntry *table;
} CTable;

COSMO_API void cosmoT_initTable(CState *state, CTable *tbl, int startCap);
COSMO_API void cosmoT_clearTable(CState *state, CTable *tbl);
COSMO_API void cosmoT_addTable(CState *state, CTable *from, CTable *to);
COSMO_API CValue *cosmoT_insert(CState *state, CTable *tbl, CValue key);

CObjString *cosmoT_lookupString(CTable *tbl, const char *str, size_t length, uint32_t hash);
bool cosmoT_get(CTable *tbl, CValue key, CValue *val);
bool cosmoT_remove(CTable *tbl, CValue key);

void cosmoT_printTable(CTable *tbl, const char *name);

#endif