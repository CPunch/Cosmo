#include "ctable.h"
#include "cmem.h"
#include "cvalue.h"
#include "cobj.h"

#include <string.h>

#define MAX_TABLE_FILL 0.75
// at 30% capacity with capacity > ARRAY_START, shrink the array
#define MIN_TABLE_CAPACITY ARRAY_START

// bit-twiddling hacks, gets the next power of 2
unsigned int nextPow2(unsigned int x) {
    if (x <= ARRAY_START - 1) return ARRAY_START; // sanity check
    x--;

    int power = 2;
    while (x >>= 1) power <<= 1;

    if (power < ARRAY_START)
        return ARRAY_START;

    return power;
}

void cosmoT_initTable(CState *state, CTable *tbl, int startCap) {
    startCap = startCap != 0 ? startCap : ARRAY_START; // sanity check :P

    tbl->capacityMask = startCap - 1;
    tbl->count = 0;
    tbl->tombstones = 0;
    tbl->table = NULL; // to let out GC know we're initalizing
    tbl->table = cosmoM_xmalloc(state, sizeof(CTableEntry) * startCap);

    // init everything to NIL
    for (int i = 0; i < startCap; i++) {
        tbl->table[i].key = cosmoV_newNil();
        tbl->table[i].val = cosmoV_newNil();
    }
}

void cosmoT_addTable(CState *state, CTable *from, CTable *to) {
    int cap = from->capacityMask + 1;
    for (int i = 0; i < cap; i++) {
        CTableEntry *entry = &from->table[i];

        if (!(IS_NIL(entry->key))) {
            CValue *newVal = cosmoT_insert(state, to, entry->key);
            *newVal = entry->val;
        }
    }
}

void cosmoT_clearTable(CState *state, CTable *tbl) {
    cosmoM_freearray(state, CTableEntry, tbl->table, (tbl->capacityMask + 1));
}

uint32_t getObjectHash(CObj *obj) {
    switch(obj->type) {
        case COBJ_STRING:
            return ((CObjString*)obj)->hash;
        default:
            return (uint32_t)obj; // just "hash" the pointer
    }
}

uint32_t getValueHash(CValue *val) {
    switch (GET_TYPE(*val)) {
        case COSMO_TREF:
            return getObjectHash(cosmoV_readRef(*val));
        case COSMO_TNUMBER: {
            uint32_t buf[sizeof(cosmo_Number)/sizeof(uint32_t)];
            cosmo_Number num = cosmoV_readNumber(*val);

            if (num == 0)
                return 0;
            
            memcpy(buf, &num, sizeof(buf));
            for (size_t i = 0; i < sizeof(cosmo_Number)/sizeof(uint32_t); i++) buf[0] += buf[i];
            return buf[0];
        }
        // TODO: add support for other types
        default:
            return 0;
    }
}

// mask should always be (capacity - 1)
static CTableEntry *findEntry(CState *state, CTableEntry *entries, int mask, CValue key) {
    uint32_t hash = getValueHash(&key);
    uint32_t indx = hash & mask; // since we know the capacity will *always* be a power of 2, we can use bitwise & to perform a MUCH faster mod operation
    CTableEntry *tomb = NULL;

    // keep looking for an open slot in the entries array
    while (true) {
        CTableEntry *entry = &entries[indx];

        if (IS_NIL(entry->key)) {
            // check if it's an empty bucket or a tombstone
            if (IS_NIL(entry->val)) {
                // it's empty! if we found a tombstone, return that so it'll be reused
                return tomb != NULL ? tomb : entry;
            } else {
                // its a tombstone!
                tomb = entry;
            }
        } else if (cosmoV_equal(state, entry->key, key)) {
            return entry;
        }

        indx = (indx + 1) & mask; // fast mod here too
    }
}

static void resizeTbl(CState *state, CTable *tbl, int newCapacity, bool canShrink) {
    if (canShrink && cosmoT_checkShrink(state, tbl))
        return;
    
    size_t size = sizeof(CTableEntry) * newCapacity;
    int cachedCount = tbl->count;
    int newCount, oldCap;

    cosmoM_checkGarbage(state, size); // if this allocation would cause a GC, run the GC

    if (tbl->count < cachedCount) // the GC removed some objects from this table and resized it, ignore our resize event!
        return;

    CTableEntry *entries = cosmoM_xmalloc(state, size);
    oldCap = tbl->capacityMask + 1;
    newCount = 0;

    // set all nodes as NIL : NIL
    for (int i = 0; i < newCapacity; i++) {
        entries[i].key = cosmoV_newNil();
        entries[i].val = cosmoV_newNil();
    }

    // move over old values to the new buffer
    for (int i = 0; i < oldCap; i++) {
        CTableEntry *oldEntry = &tbl->table[i];
        if (IS_NIL(oldEntry->key))
            continue; // skip empty keys

        // get new entry location & update the node
        CTableEntry *newEntry = findEntry(state, entries, newCapacity - 1, oldEntry->key);
        newEntry->key = oldEntry->key;
        newEntry->val = oldEntry->val;
        newCount++; // inc count
    }

    // free the old table
    cosmoM_freearray(state, CTableEntry, tbl->table, oldCap);

    tbl->table = entries;
    tbl->capacityMask = newCapacity - 1;
    tbl->count = newCount;
    tbl->tombstones = 0;
}

bool cosmoT_checkShrink(CState *state, CTable *tbl) {
    // if count > 8 and active entries < tombstones 
    if (tbl->count > MIN_TABLE_CAPACITY && (tbl->count - tbl->tombstones < tbl->tombstones || tbl->tombstones > 50)) { // TODO: 50 should be a threshhold
        resizeTbl(state, tbl, nextPow2(tbl->count - tbl->tombstones) * GROW_FACTOR, false); // shrink based on active entries to the next pow of 2
        return true;
    }

    return false;
}

// returns a pointer to the allocated value
COSMO_API CValue* cosmoT_insert(CState *state, CTable *tbl, CValue key) {
    // make sure we have enough space allocated
    int cap = tbl->capacityMask + 1;
    if (tbl->count + 1 > (int)(cap * MAX_TABLE_FILL)) {
        // grow table
        int newCap = cap * GROW_FACTOR;
        resizeTbl(state, tbl, newCap, true);
    }

    // insert into the table
    CTableEntry *entry = findEntry(state, tbl->table, tbl->capacityMask, key); // -1 for our capacity mask

    if (IS_NIL(entry->key)) {
        if (IS_NIL(entry->val)) // is it empty?
            tbl->count++;
        else // it's a tombstone, mark it alive!
            tbl->tombstones--;
    }

    entry->key = key;
    return &entry->val;
}

bool cosmoT_get(CState *state, CTable *tbl, CValue key, CValue *val) {
    // sanity check
    if (tbl->count == 0) {
        *val = cosmoV_newNil();
        return false;
    }
    
    CTableEntry *entry = findEntry(state, tbl->table, tbl->capacityMask, key);
    *val = entry->val;
    
    // return if get was successful
    return !(IS_NIL(entry->key));
}

bool cosmoT_remove(CState* state, CTable *tbl, CValue key) {
    if (tbl->count == 0) return 0; // sanity check

    CTableEntry *entry = findEntry(state, tbl->table, tbl->capacityMask, key);
    if (IS_NIL(entry->key)) // sanity check
        return false;

    // crafts tombstone
    entry->key = cosmoV_newNil(); // this has to be nil
    entry->val = cosmoV_newBoolean(false); // doesn't really matter what this is, as long as it isn't nil
    tbl->tombstones++;

    return true;
}

// returns the active entry count
COSMO_API int cosmoT_count(CTable *tbl) {
    return tbl->count - tbl->tombstones;
}

CObjString *cosmoT_lookupString(CTable *tbl, const char *str, int length, uint32_t hash) {
    if (tbl->count == 0) return 0; // sanity check
    uint32_t indx = hash & tbl->capacityMask; // since we know the capacity will *always* be a power of 2, we can use bitwise & to perform a MUCH faster mod operation

    // keep looking for an open slot in the entries array
    while (true) {
        CTableEntry *entry = &tbl->table[indx];

        // check if it's an empty slot (meaning we dont have it in the table)
        if (IS_NIL(entry->key) && IS_NIL(entry->val)) {
            return NULL;
        } else if (IS_STRING(entry->key) && cosmoV_readString(entry->key)->length == length && memcmp(cosmoV_readString(entry->key)->str, str, length) == 0) {
            // it's a match!
            return (CObjString*)cosmoV_readRef(entry->key);
        }

        indx = (indx + 1) & tbl->capacityMask; // fast mod here too
    }
}

// for debugging purposes
void cosmoT_printTable(CTable *tbl, const char *name) {
    printf("==== [[%s]] ====\n", name);
    int cap = tbl->capacityMask + 1;
    for (int i = 0; i < cap; i++) {
        CTableEntry *entry = &tbl->table[i];
        if (!(IS_NIL(entry->key))) {
            printValue(entry->key);
            printf(" - ");
            printValue(entry->val);
            printf("\n");
        }
    }
}
