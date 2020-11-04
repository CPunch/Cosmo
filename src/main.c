#include "cosmo.h"
#include "cchunk.h"
#include "cdebug.h"
#include "cvm.h"
#include "cparse.h"
#include "cbaselib.h"

#include "cmem.h"

static bool _ACTIVE;

int cosmoB_quitRepl(CState *state, int nargs, CValue *args) {
    _ACTIVE = false;

    return 0; // we don't do anything to the stack
}

static void interpret(CState *state, const char* script) {
    
    // cosmoP_compileString pushes the result onto the stack (NIL or COBJ_FUNCTION)
    CObjFunction* func = cosmoP_compileString(state, script);
    if (func != NULL) {
        disasmChunk(&func->chunk, "_main", 0);
        
        cosmoV_call(state, 0, 0); // 0 args being passed, 0 results expected

        //cosmoV_printStack(state);
        //cosmoT_printTable(&state->globals, "globals");
        //cosmoT_printTable(&state->strings, "strings");
    }
}

static void repl() {
    char line[1024];
    _ACTIVE = true;

    CState *state = cosmoV_newState();
    cosmoB_loadlibrary(state);

    // TODO: there's gotta be a better way to do this
    cosmoV_register(state, "quit", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_quitRepl)));

    while (_ACTIVE) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) { // better than gets()
            printf("\n> ");
            break;
        }

        interpret(state, line);
    }

    cosmoV_freeState(state);
}

static char *readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    // first, we need to know how big our file is
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char*)malloc(fileSize + 1); // make room for the null byte
    if (buffer == NULL) {
        fprintf(stderr, "failed to allocate!");
        exit(1);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

    if (bytesRead < fileSize) {
        printf("failed to read file \"%s\"!\n", path);
        exit(74);
    }
    
    buffer[bytesRead] = '\0'; // place our null terminator

    // close the file handler and return the script buffer
    fclose(file);
    return buffer;
}

static void runFile(const char* fileName) {
    char* script = readFile(fileName);
    CState *state = cosmoV_newState();
    cosmoB_loadlibrary(state);

    interpret(state, script);

    cosmoV_freeState(state);
    free(script);
}

int main(int argc, const char *argv[]) {

    //interpret("\"hello world!\"");

    if (argc == 1) {
        repl();
    } else if (argc >= 2) { // they passed a file (or more lol)
        for (int i = 1; i < argc; i++) {
            runFile(argv[i]);
        }
    }


    /*
    CChunk *chnk = newChunk(1);
    CState *state = cosmoV_newState();

    // adds our constant values
    int constIndx = addConstant(chnk, cosmoV_newNumber(2));
    int const2Indx = addConstant(chnk, cosmoV_newNumber(4));
    
    // pushes constant to the stack
    writeu8Chunk(chnk, OP_LOADCONST, 1);
    writeu16Chunk(chnk, constIndx, 1);

    writeu8Chunk(chnk, OP_LOADCONST, 1);
    writeu16Chunk(chnk, const2Indx, 1);

    // pops 2 values off the stack, multiples them together and pushes the result
    writeu8Chunk(chnk, OP_MULT, 1);

    // pops a value off the stack, negates it, and pushes the result
    writeu8Chunk(chnk, OP_NEGATE, 2);

    // prints to the console
    writeu8Chunk(chnk, OP_RETURN, 2);
    disasmChunk(chnk, "test");

    // load chunk to the state & run it
    cosmoV_loadChunk(state, chnk);
    cosmoV_execute(state, 0);

    // clean up :)
    freeChunk(chnk);
    cosmoV_freeState(state);*/

    return 0;
}