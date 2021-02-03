#include "cosmo.h"
#include "cchunk.h"
#include "cdebug.h"
#include "cvm.h"
#include "cparse.h"
#include "cbaselib.h"

#include "cmem.h"

static bool _ACTIVE = false;

int cosmoB_quitRepl(CState *state, int nargs, CValue *args) {
    _ACTIVE = false;

    return 0; // we don't return anything
}

int cosmoB_input(CState *state, int nargs, CValue *args) {
    // input() accepts the same params as print()!
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }

    // but, we return user input instead!
    char line[1024];
    fgets(line, sizeof(line), stdin);

    cosmoV_pushObj(state, (CObj*)cosmoO_copyString(state, line, strlen(line)-1)); // -1 for the \n

    return 1; // 1 return value
}

static void interpret(CState *state, const char *script, const char *mod) {
    // cosmoV_compileString pushes the result onto the stack (COBJ_ERROR or COBJ_CLOSURE)
    if (cosmoV_compileString(state, script, mod)) {        
        COSMOVMRESULT res = cosmoV_call(state, 0, 0); // 0 args being passed, 0 results expected

        if (res == COSMOVM_RUNTIME_ERR)
            cosmoV_printError(state, state->error);
    } else {
        cosmoV_pop(state); // pop the error off the stack
        cosmoV_printError(state, state->error);
    }

    state->panic = false; // so our repl isn't broken
}

static void repl() {
    char line[1024];
    _ACTIVE = true;

    CState *state = cosmoV_newState();
    cosmoB_loadLibrary(state);
    cosmoB_loadVM(state);

    // add our custom REPL functions
    cosmoV_pushString(state, "quit");
    cosmoV_pushCFunction(state, cosmoB_quitRepl);

    cosmoV_pushString(state, "input");
    cosmoV_pushCFunction(state, cosmoB_input);

    cosmoV_register(state, 2);

    while (_ACTIVE) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) { // better than gets()
            printf("\n> ");
            break;
        }

        interpret(state, line, "REPL");
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
    cosmoB_loadLibrary(state);

    // add our input() function to the global table
    cosmoV_pushString(state, "input");
    cosmoV_pushCFunction(state, cosmoB_input);

    cosmoV_register(state, 1);

    interpret(state, script, fileName);

    cosmoV_freeState(state);
    free(script);
}

int main(int argc, const char *argv[]) {
    if (argc == 1) {
        repl();
    } else if (argc >= 2) { // they passed a file (or more lol)
        for (int i = 1; i < argc; i++) {
            runFile(argv[i]);
        }
    }

    return 0;
}
