#ifndef CDEBUG_H
#define CDEBUG_H

#include "cchunk.h"
#include "cobj.h"

COSMO_API void disasmChunk(CChunk *chunk, const char *name, int indent);
COSMO_API int disasmInstr(CChunk *chunk, int offset, int indent);

void printIndent(int indent);

COSMO_API void cosmoG_disassemble(CObjClosure *closure);

#endif
