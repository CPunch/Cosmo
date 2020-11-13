#include "cosmo.h"
#include "cchunk.h"
#include "cdebug.h"
#include "cvm.h"
#include "cparse.h"
#include "cbaselib.h"

#include "cmem.h"

static bool _ACTIVE = false;

CValue cosmoB_quitRepl(CState *state, int nargs, CValue *args) {
    _ACTIVE = false;

    return cosmoV_newNil(); // we don't return anything
}

CValue cosmoB_input(CState *state, int nargs, CValue *args) {
    // input() accepts the same params as print()!
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }

    // but, we return user input instead!
    char line[1024];
    fgets(line, sizeof(line), stdin);

    return cosmoV_newObj(cosmoO_copyString(state, line, strlen(line)-1)); // -1 for the \n
}

static void interpret(CState *state, const char* script) {
    
    // cosmoP_compileString pushes the result onto the stack (NIL or COBJ_FUNCTION)
    CObjFunction* func = cosmoP_compileString(state, script);
    if (func != NULL) {
        disasmChunk(&func->chunk, "_main", 0);
        
        COSMOVMRESULT res = cosmoV_call(state, 0); // 0 args being passed

        if (res == COSMOVM_RUNTIME_ERR)
            state->panic = false; // so our repl isn't broken
    }
}

static void repl() {
    char line[1024];
    _ACTIVE = true;

    CState *state = cosmoV_newState();
    cosmoB_loadLibrary(state);
    cosmoB_loadDebug(state);

    // TODO: there's gotta be a better way to do this
    cosmoV_register(state, "quit", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_quitRepl)));
    cosmoV_register(state, "input", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_input)));

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
    cosmoB_loadLibrary(state);

    cosmoV_register(state, "input", cosmoV_newObj(cosmoO_newCFunction(state, cosmoB_input)));

    interpret(state, script);

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