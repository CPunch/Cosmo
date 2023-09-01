#include "cbaselib.h"
#include "cchunk.h"
#include "cdebug.h"
#include "cdump.h"
#include "cmem.h"
#include "cosmo.h"
#include "cparse.h"
#include "cundump.h"
#include "cvm.h"

#include "util/linenoise.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#    include "util/getopt.h"
#else
#    include <getopt.h>
#endif

static bool _ACTIVE = false;

int cosmoB_quitRepl(CState *state, int nargs, CValue *args)
{
    _ACTIVE = false;

    return 0; // we don't return anything
}

int cosmoB_input(CState *state, int nargs, CValue *args)
{
    // input() accepts the same params as print()!
    for (int i = 0; i < nargs; i++) {
        CObjString *str = cosmoV_toString(state, args[i]);
        printf("%s", cosmoO_readCString(str));
    }

    // but, we return user input instead!
    char line[1024];
    fgets(line, sizeof(line), stdin);

    cosmoV_pushRef(state,
                   (CObj *)cosmoO_copyString(state, line, strlen(line) - 1)); // -1 for the \n

    return 1; // 1 return value
}

static bool interpret(CState *state, const char *script, const char *mod)
{

    // cosmoV_compileString pushes the result onto the stack (COBJ_ERROR or COBJ_CLOSURE)
    if (cosmoV_compileString(state, script, mod)) {
        // 0 args being passed, 0 results expected
        if (!cosmoV_pcall(state, 0, 0)) {
            cosmoV_printError(state, cosmoV_readError(*cosmoV_pop(state)));
            return false;
        }
    } else {
        cosmoV_printError(state, cosmoV_readError(*cosmoV_pop(state)));
        return false;
    }

    return true;
}

static void repl(CState *state)
{
    char *line;
    _ACTIVE = true;

    // add our custom REPL functions
    cosmoV_pushString(state, "quit");
    cosmoV_pushCFunction(state, cosmoB_quitRepl);

    cosmoV_pushString(state, "input");
    cosmoV_pushCFunction(state, cosmoB_input);

    cosmoV_register(state, 2);

    while (_ACTIVE) {
        if (!(line = linenoise("> "))) { // better than gets()
            break;
        }

        linenoiseHistoryAdd(line);
        interpret(state, line, "REPL");
        linenoiseFree(line);
    }
}

static char *readFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    // first, we need to know how big our file is
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(fileSize + 1); // make room for the null byte
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

static bool runFile(CState *state, const char *fileName)
{
    bool ret;
    char *script = readFile(fileName);

    // add our input() function to the global table
    cosmoV_pushString(state, "input");
    cosmoV_pushCFunction(state, cosmoB_input);

    cosmoV_register(state, 1);

    ret = interpret(state, script, fileName);

    free(script);
    return ret; // let the caller know if the script failed
}

int fileWriter(CState *state, const void *data, size_t size, const void *ud)
{
    return !fwrite(data, size, 1, (FILE *)ud);
}

int fileReader(CState *state, void *data, size_t size, const void *ud)
{
    return fread(data, size, 1, (FILE *)ud) != 1;
}

void compileScript(CState *state, const char *in, const char *out)
{
    char *script = readFile(in);

    FILE *fout = fopen(out, "wb");

    if (cosmoV_compileString(state, script, in)) {
        CObjFunction *func = cosmoV_readClosure(*cosmoV_getTop(state, 0))->function;
        cosmoD_dump(state, func, fileWriter, (void *)fout);
    } else {
        cosmoV_printError(state, cosmoV_readError(*cosmoV_pop(state)));
    }

    free(script);
    fclose(fout);

    printf("[!] compiled %s to %s successfully!\n", in, out);
}

void loadScript(CState *state, const char *in)
{
    FILE *file = fopen(in, "rb");
    if (!cosmoV_undump(state, fileReader, file)) {
        cosmoV_printError(state, cosmoV_readError(*cosmoV_pop(state)));
        return;
    };

    printf("[!] loaded %s!\n", in);
    if (!cosmoV_pcall(state, 0, 0))
        cosmoV_printError(state, cosmoV_readError(*cosmoV_pop(state)));

    fclose(file);
}

void printUsage(const char *name)
{
    printf("Usage: %s [-clsr] [args]\n\n", name);
    printf("available options are:\n"
           "-c <in> <out>\tcompile <in> and dump to <out>\n"
           "-l <in>\t\tload dump from <in>\n"
           "-s <in...>\tcompile and run <in...> script(s)\n"
           "-r\t\tstart the repl\n\n");
}

int main(int argc, char *const argv[])
{
    CState *state = cosmoV_newState();
    cosmoB_loadLibrary(state);
    cosmoB_loadOS(state);
    cosmoB_loadVM(state);

    int opt;
    bool isValid = false;
    while ((opt = getopt(argc, argv, "clsr")) != -1) {
        switch (opt) {
        case 'c':
            if (optind >= argc - 1) {
                printf("Usage: %s -c <in> <out>\n", argv[0]);
                exit(EXIT_FAILURE);
            } else {
                compileScript(state, argv[optind], argv[optind + 1]);
            }
            isValid = true;
            break;
        case 'l':
            if (optind >= argc) {
                printf("Usage: %s -l <in>\n", argv[0]);
                exit(EXIT_FAILURE);
            } else {
                loadScript(state, argv[optind]);
            }
            isValid = true;
            break;
        case 's':
            for (int i = optind; i < argc; i++) {
                if (!runFile(state, argv[i])) {
                    printf("failed to run %s!\n", argv[i]);
                    exit(EXIT_FAILURE);
                }
            }
            isValid = true;
            break;
        case 'r':
            repl(state);
            isValid = true;
            break;
        }
    }

    if (!isValid) {
        printUsage(argv[0]);
    }

    cosmoV_freeState(state);
    return 0;
}
