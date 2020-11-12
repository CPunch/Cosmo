#include "cparse.h"
#include "cstate.h"
#include "clex.h"
#include "cchunk.h"
#include "cdebug.h"
#include "cmem.h"

#include <string.h>

// we define all of this here because we only need it in this file, no need for it to be in the header /shrug

typedef struct {
    CToken name;
    int depth;
    bool isCaptured; // is the Local referenced in an upvalue?
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    FTYPE_FUNCTION,
    FTYPE_METHOD, // a function bounded to an object (can use "this" identifer to access the current object :pog:)
    FTYPE_SCRIPT
} FunctionType;

typedef struct CCompilerState {
    CObjFunction *function;
    FunctionType type;

    Local locals[256];
    Upvalue upvalues[256];
    int localCount;
    int scopeDepth;
    int pushedValues;
    int savedPushed;
    struct CCompilerState* enclosing;
} CCompilerState;

typedef struct {
    CLexState *lex;
    CCompilerState* compiler;
    CState *state;
    CToken current;
    CToken previous; // token right after the current token
    bool hadError;
    bool panic;
} CParseState;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_CONCAT,        // ..
    PREC_OR,            // or
    PREC_AND,           // and
    PREC_EQUALITY,      // == !=
    PREC_COMPARISON,    // < > <= >=
    PREC_TERM,          // + -
    PREC_FACTOR,        // * /
    PREC_UNARY,         // ! -
    PREC_CALL,          // . ()
    PREC_PRIMARY        // everything else
} Precedence;

typedef void (*ParseFunc)(CParseState* pstate, bool canAssign);

typedef struct {
    ParseFunc prefix;
    ParseFunc infix;
    Precedence level;
} ParseRule;

static void parsePrecedence(CParseState*, Precedence);
static void variable(CParseState *pstate, bool canAssign);
static void expression(CParseState*);
static void statement(CParseState *pstate);
static void declaration(CParseState *pstate);
static void function(CParseState *pstate, FunctionType type);
static void expressionStatement(CParseState *pstate);
static ParseRule* getRule(CTokenType type);
static CObjFunction *endCompiler(CParseState *pstate);

// ================================================================ [FRONT END/TALK TO LEXER] ================================================================

static void initCompilerState(CParseState* pstate, CCompilerState *ccstate, FunctionType type, CCompilerState *enclosing) {
    pstate->compiler = ccstate;

    ccstate->enclosing = enclosing;
    ccstate->function = NULL;
    ccstate->localCount = 0;
    ccstate->scopeDepth = 0;
    ccstate->pushedValues = 0;
    ccstate->savedPushed = 0;
    ccstate->type = type;
    ccstate->function = cosmoO_newFunction(pstate->state);

    if (type != FTYPE_SCRIPT) 
        ccstate->function->name = cosmoO_copyString(pstate->state, pstate->previous.start, pstate->previous.length);

    // mark first local slot as used (this'll hold the CObjFunction of the current function, or if it's a method it'll hold the currently bounded object)
    Local *local = &ccstate->locals[ccstate->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

static void initParseState(CParseState *pstate, CCompilerState *ccstate, CState *s, const char *source) {
    pstate->lex = cosmoL_newLexState(s, source);

    pstate->state = s;
    pstate->hadError = false;
    pstate->panic = false;
    pstate->compiler = ccstate;
    
    initCompilerState(pstate, ccstate, FTYPE_SCRIPT, NULL); // enclosing starts as NULL
}

static void freeParseState(CParseState *pstate) {
    cosmoL_freeLexState(pstate->state, pstate->lex);
}

static void errorAt(CParseState *pstate, CToken *token, const char * msg) {
    if (pstate->hadError)
        return;

    fprintf(stderr, "[line %d] Objection", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {

    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    printf(": \n\t%s\n", msg);
    pstate->hadError = true;
    pstate->panic = true;
}

static void errorAtCurrent(CParseState *pstate, const char *msg) {
    errorAt(pstate, &pstate->current, msg);
}

static void error(CParseState *pstate, const char *msg) {
    errorAt(pstate, &pstate->previous, msg);
}

static void advance(CParseState *pstate) {
    pstate->previous = pstate->current;
    pstate->current = cosmoL_scanToken(pstate->lex);

    //printf("got %d [%.*s]\n", pstate->current.type, pstate->current.length, pstate->current.start);

    if (pstate->current.type == TOKEN_ERROR) {
        // go ahead and consume the rest of the errors so it doesn't cascade
        CToken temp;
        do {
            temp = cosmoL_scanToken(pstate->lex);
        } while(temp.type == TOKEN_ERROR);
    }
}

static bool check(CParseState *pstate, CTokenType type) {
    return pstate->current.type == type;
}

// consumes the next token if it matches type, otherwise errors
static void consume(CParseState* pstate, CTokenType type, const char *msg) {
    if (pstate->current.type == type) { // if token matches, consume the next token
        advance(pstate);
        return;
    }

    errorAtCurrent(pstate, msg);
}

static bool match(CParseState *pstate, CTokenType type) {
    if (!check(pstate, type))
        return false;

    // if it matched, go ahead and consume the next token
    advance(pstate);
    return true;
}

static bool identifiersEqual(CToken *idA, CToken *idB) {
    return idA->length == idB->length && memcmp(idA->start, idB->start, idA->length) == 0;
}

static void inline valuePushed(CParseState *pstate, int values) {
    pstate->compiler->pushedValues += values;
}

static void inline valuePopped(CParseState *pstate, int values) {
    pstate->compiler->pushedValues -= values;
}

static bool blockFollow(CToken token) {
    switch (token.type) {
        case TOKEN_END: case TOKEN_ELSE:
        case TOKEN_ELSEIF: case TOKEN_EOS:
            return true;
        default:
            return false;
    }
}

// ================================================================ [WRITE TO CHUNK] ================================================================

CChunk* getChunk(CParseState *pstate) {
    return &pstate->compiler->function->chunk;
}

// safely adds constant to chunk, checking for overflow
uint16_t makeConstant(CParseState *pstate, CValue val) {
    int indx = addConstant(pstate->state, getChunk(pstate), val);
    if (indx > UINT16_MAX) {
        error(pstate, "UInt overflow! Too many constants in one chunk!");
        return 0;
    }

    return (uint16_t)indx;
}

void writeu8(CParseState *pstate, INSTRUCTION i) {
    writeu8Chunk(pstate->state, getChunk(pstate), i, pstate->previous.line);
}

void writeu16(CParseState *pstate, uint16_t i) {
    writeu16Chunk(pstate->state, getChunk(pstate), i, pstate->previous.line);
}

void writeConstant(CParseState *pstate, CValue val) {
    writeu8(pstate, OP_LOADCONST);
    writeu16(pstate, makeConstant(pstate, val));

    valuePushed(pstate, 1);
}

int writeJmp(CParseState *pstate, INSTRUCTION i) {
    writeu8(pstate, i);
    writeu16(pstate, 0xFFFF);

    return getChunk(pstate)->count - 2;
}

void writePop(CParseState *pstate, int times) {
    writeu8(pstate, OP_POP);
    writeu8(pstate, times);
}

void writeJmpBack(CParseState *pstate, int location) {
    int jmp = (getChunk(pstate)->count - location) + 3;

    if (jmp > UINT16_MAX)
        error(pstate, "UInt overflow! Too much code to jump!");

    writeu8(pstate, OP_JMPBACK);
    writeu16(pstate, jmp);
}

// patches offset operand at location
void patchJmp(CParseState *pstate, int index) {
    unsigned int jump = getChunk(pstate)->count - index - 2;

    if (jump > UINT16_MAX)
        error(pstate, "UInt overflow! Too much code to jump!");

    memcpy(&getChunk(pstate)->buf[index], &jump, sizeof(uint16_t));
}

static uint16_t identifierConstant(CParseState *pstate, CToken *name) {
  return makeConstant(pstate, cosmoV_newObj((CObj*)cosmoO_copyString(pstate->state, name->start, name->length)));
}

static void addLocal(CParseState *pstate, CToken name) {
    if (pstate->compiler->localCount > UINT8_MAX)
        return error(pstate, "UInt overflow! Too many locals in scope!");

    Local *local = &pstate->compiler->locals[pstate->compiler->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static int addUpvalue(CCompilerState *ccstate, uint8_t indx, bool isLocal) {
    int upvals = ccstate->function->upvals;

    // check and make sure we haven't already captured it
    for (int i = 0; i < upvals; i++) {
        Upvalue *upval = &ccstate->upvalues[i];
        if (upval->index == indx && upval->isLocal == isLocal) // it matches! return that
            return i;
    }

    // TODO: throw error if upvals >= UINT8_MAX

    ccstate->upvalues[upvals].index = indx;
    ccstate->upvalues[upvals].isLocal = isLocal;
    return ccstate->function->upvals++;
}

static int getLocal(CCompilerState *ccstate, CToken *name) {
    for (int i = ccstate->localCount - 1; i >= 0; i--) {
        Local *local = &ccstate->locals[i];
        if (local->depth != -1 && identifiersEqual(name, &local->name)) { // if the identifer is initalized and it matches, use it!
            return i;
        }
    }

    // it wasn't found
    return -1;
}

static int getUpvalue(CCompilerState *ccstate, CToken *name) {
    if (ccstate->enclosing == NULL) // there's no upvalues to lookup!
        return -1;

    int local = getLocal(ccstate->enclosing, name);
    if (local != -1) {
        ccstate->enclosing->locals[local].isCaptured = true;
        return addUpvalue(ccstate, local, true);
    }

    int upval = getUpvalue(ccstate->enclosing, name);
    if (upval != -1)
        return addUpvalue(ccstate, upval, false);

    return -1; // failed!
}

static void markInitialized(CParseState *pstate, int local) {
    pstate->compiler->locals[local].depth = pstate->compiler->scopeDepth;
}

static int parseArguments(CParseState *pstate) {
    int args = 0;

    // there are args to parse!
    if (!check(pstate, TOKEN_RIGHT_PAREN)) {
        do {
            expression(pstate);
            args++;
        } while(match(pstate, TOKEN_COMMA));
    }
    consume(pstate, TOKEN_RIGHT_PAREN, "Expected ')' to end call.");

    // sanity check
    if (args > UINT8_MAX) {
        errorAtCurrent(pstate, "Too many arguments passed in call.");
    }
    return args;
}

// recovers stack (pops unneeded values, reports missing values)
static void alignStack(CParseState *pstate, int alignment) {
    // realign the stack
    if (pstate->compiler->pushedValues > alignment) {
        writePop(pstate, pstate->compiler->pushedValues - alignment);
    } else if (pstate->compiler->pushedValues < alignment) {
        error(pstate, "Missing expression!");
    }

    pstate->compiler->pushedValues = alignment;
}

// ================================================================ [PRATT'S PARSER] ================================================================

static void number(CParseState *pstate, bool canAssign) {
    cosmo_Number num = strtod(pstate->previous.start, NULL);
    writeConstant(pstate, cosmoV_newNumber(num));
}

static void string(CParseState *pstate, bool canAssign) {
    CObjString *strObj = cosmoO_copyString(pstate->state, pstate->previous.start + 1, pstate->previous.length - 2);
    writeConstant(pstate, cosmoV_newObj((CObj*)strObj));
}

static void literal(CParseState *pstate, bool canAssign) {
    switch (pstate->previous.type) {
        case TOKEN_TRUE:    writeu8(pstate, OP_TRUE); break;
        case TOKEN_FALSE:   writeu8(pstate, OP_FALSE); break;
        case TOKEN_NIL:     writeu8(pstate, OP_NIL); break;
        default:
            break;
    }

    valuePushed(pstate, 1);
}

// parses prefix operators
static void unary(CParseState *pstate, bool canAssign) {
    CTokenType type = pstate->previous.type;
    int cachedLine = pstate->previous.line; // eval'ing the next expression might change the line number

    // only eval the next *value*
    parsePrecedence(pstate, PREC_UNARY);

    switch(type) {
        case TOKEN_MINUS:   writeu8Chunk(pstate->state, getChunk(pstate), OP_NEGATE, cachedLine); break;
        case TOKEN_BANG:    writeu8Chunk(pstate->state, getChunk(pstate), OP_NOT, cachedLine); break;
        default:
            error(pstate, "Unexpected unary operator!");
    }
}

// parses infix operators
static void binary(CParseState *pstate, bool canAssign) {
    CTokenType type = pstate->previous.type; // already consumed
    int cachedLine = pstate->previous.line; // eval'ing the next expression might change the line number

    parsePrecedence(pstate, getRule(type)->level + 1);

    switch (type) {
        // ARITH
        case TOKEN_PLUS:    writeu8Chunk(pstate->state, getChunk(pstate), OP_ADD, cachedLine); break;
        case TOKEN_MINUS:   writeu8Chunk(pstate->state, getChunk(pstate), OP_SUB, cachedLine); break;
        case TOKEN_STAR:    writeu8Chunk(pstate->state, getChunk(pstate), OP_MULT, cachedLine); break;
        case TOKEN_SLASH:   writeu8Chunk(pstate->state, getChunk(pstate), OP_DIV, cachedLine); break;
        // EQUALITY
        case TOKEN_EQUAL_EQUAL:     writeu8Chunk(pstate->state, getChunk(pstate), OP_EQUAL, cachedLine); break;
        case TOKEN_GREATER:         writeu8Chunk(pstate->state, getChunk(pstate), OP_GREATER, cachedLine); break;
        case TOKEN_LESS:            writeu8Chunk(pstate->state, getChunk(pstate), OP_LESS, cachedLine); break;
        case TOKEN_GREATER_EQUAL:   writeu8Chunk(pstate->state, getChunk(pstate), OP_GREATER_EQUAL, cachedLine); break;
        case TOKEN_LESS_EQUAL:      writeu8Chunk(pstate->state, getChunk(pstate), OP_LESS_EQUAL, cachedLine); break;
        case TOKEN_BANG_EQUAL:      writeu8Chunk(pstate->state, getChunk(pstate), OP_EQUAL, cachedLine); writeu8Chunk(pstate->state, getChunk(pstate), OP_NOT, cachedLine); break;
        default:
            error(pstate, "Unexpected operator!");
    }

    valuePopped(pstate, 1); // we pop 2 values off the stack and push 1 for a net pop of 1 value
}

static void group(CParseState *pstate, bool canAssign) {
    expression(pstate);
    consume(pstate, TOKEN_RIGHT_PAREN, "Expected ')'");
}

static void _etterOP(CParseState *pstate, uint8_t op, int arg) {
    writeu8(pstate, op);
    if (op == OP_GETGLOBAL || op == OP_SETGLOBAL) // globals are stored with a u16
        writeu16(pstate, arg);
    else
        writeu8(pstate, arg);
}

static void namedVariable(CParseState *pstate, CToken name, bool canAssign) {
    uint8_t opGet, opSet;
    int arg = getLocal(pstate->compiler, &name);

    if (arg != -1) {
        // we found it in out local table!
        opGet = OP_GETLOCAL;
        opSet = OP_SETLOCAL;
    } else if ((arg = getUpvalue(pstate->compiler, &name)) != -1) {
        opGet = OP_GETUPVAL;
        opSet = OP_SETUPVAL;
    } else {
        // local & upvalue wasnt' found, assume it's a global!
        arg = identifierConstant(pstate, &name);
        opGet = OP_GETGLOBAL;
        opSet = OP_SETGLOBAL;
    }

    if (canAssign && match(pstate, TOKEN_EQUAL)) {
        // setter
        expression(pstate);
        _etterOP(pstate, opSet, arg);
        valuePopped(pstate, 1);
    } else {
        // getter
        _etterOP(pstate, opGet, arg);
        valuePushed(pstate, 1);
    }
}

static void and_(CParseState *pstate, bool canAssign) {
    int jump = writeJmp(pstate, OP_EJMP); // conditional jump without popping

    writePop(pstate, 1);
    parsePrecedence(pstate, PREC_AND);

    patchJmp(pstate, jump);
}

static void or_(CParseState *pstate, bool canAssign) {
    int elseJump = writeJmp(pstate, OP_EJMP);
    int endJump = writeJmp(pstate, OP_JMP);

    patchJmp(pstate, elseJump);
    writePop(pstate, 1);

    parsePrecedence(pstate, PREC_OR);
    
    patchJmp(pstate, endJump);
}

static void anonFunction(CParseState *pstate, bool canAssign) {
    function(pstate, FTYPE_FUNCTION);
}

static void variable(CParseState *pstate, bool canAssign) {
    namedVariable(pstate, pstate->previous, canAssign);
}

static void concat(CParseState *pstate, bool canAssign) {
    CTokenType type = pstate->previous.type;

    int vars = 1; // we already have something on the stack
    do {
        parsePrecedence(pstate, getRule(type)->level + 1); // parse until next concat
        vars++;
    } while (match(pstate, TOKEN_DOT_DOT));

    writeu8(pstate, OP_CONCAT);
    writeu8(pstate, vars);

    valuePopped(pstate, vars - 1); // - 1 because we're pushing the concat result
}

static void call_(CParseState *pstate, bool canAssign) {
    // we enter having already consumed the '('

    // grab our arguments
    uint8_t argCount = parseArguments(pstate);
    valuePopped(pstate, argCount + 1); // all of these values will be popped off the stack when returned (+1 for the function)
    writeu8(pstate, OP_CALL);
    writeu8(pstate, argCount);
    valuePushed(pstate, 1);
}

static void object(CParseState *pstate, bool canAssign) {
    // already consumed the beginning '{'
    int entries = 0;

    if (!match(pstate, TOKEN_RIGHT_BRACE)) {
        do {
            if (match(pstate, TOKEN_IDENTIFIER)) {
                uint16_t fieldIdent = identifierConstant(pstate, &pstate->previous);

                // OP_NEWOBJECT expects the key on the stack before the value
                writeu8(pstate, OP_LOADCONST);
                writeu16(pstate, fieldIdent);

                consume(pstate, TOKEN_EQUAL, "Invalid syntax!");

                // parse field
                expression(pstate);
                valuePopped(pstate, 1);
            } else if (match(pstate, TOKEN_LEFT_BRACKET)) {
                // parse the key first
                expression(pstate); // should parse until end bracket

                consume(pstate, TOKEN_RIGHT_BRACKET, "Expected ']' to end index definition.");
                consume(pstate, TOKEN_EQUAL, "Expected '='.");

                // now, parse the value (until comma)
                expression(pstate);
                valuePopped(pstate, 2);
            } else {
                error(pstate, "Invalid syntax!");
            }
            
            entries++;
        } while (match(pstate, TOKEN_COMMA) && !pstate->hadError);

        consume(pstate, TOKEN_RIGHT_BRACE, "Expected '}' to end object definition.");
    }

    writeu8(pstate, OP_NEWOBJECT);
    writeu8(pstate, entries);
    valuePushed(pstate, 1);
}

static void dot(CParseState *pstate, bool canAssign) {
    consume(pstate, TOKEN_IDENTIFIER, "Expected property name after '.'.");
    uint16_t name = identifierConstant(pstate, &pstate->previous);
    writeu8(pstate, OP_LOADCONST);
    writeu16(pstate, name);

    if (canAssign && match(pstate, TOKEN_EQUAL)) {
        expression(pstate);
        writeu8(pstate, OP_SETOBJECT);
        valuePopped(pstate, 2); // pops key, value & object
    } else {
        writeu8(pstate, OP_GETOBJECT);
        // pops key & object but also pushes the field so total popped is 1
    }
}

static void _index(CParseState *pstate, bool canAssign) {
    expression(pstate);
    consume(pstate, TOKEN_RIGHT_BRACKET, "Expected ']' to end index.");

    if (canAssign && match(pstate, TOKEN_EQUAL)) {
        expression(pstate);
        writeu8(pstate, OP_SETOBJECT);
        valuePopped(pstate, 3); // pops key, value & object
    } else {
        writeu8(pstate, OP_GETOBJECT);
        valuePopped(pstate, 1); // pops key & object but also pushes the field so total popped is 1
    }
}

ParseRule ruleTable[] = {
    [TOKEN_LEFT_PAREN]      = {group, call_, PREC_CALL},
    [TOKEN_RIGHT_PAREN]     = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]      = {object, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]     = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]    = {NULL, _index, PREC_CALL},
    [TOKEN_RIGHT_BRACKET]   = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]           = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT]             = {NULL, dot, PREC_CALL},
    [TOKEN_DOT_DOT]         = {NULL, concat, PREC_CONCAT},
    [TOKEN_MINUS]           = {unary, binary, PREC_TERM},
    [TOKEN_PLUS]            = {NULL, binary, PREC_TERM},
    [TOKEN_SLASH]           = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR]            = {NULL, binary, PREC_FACTOR},
    [TOKEN_EOS]             = {NULL, NULL, PREC_NONE},
    [TOKEN_BANG]            = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL]      = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL]           = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL]     = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER]         = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]   = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS]            = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]      = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]      = {variable, NULL, PREC_NONE},
    [TOKEN_STRING]          = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER]          = {number, NULL, PREC_NONE},
    [TOKEN_NIL]             = {literal, NULL, PREC_NONE},
    [TOKEN_TRUE]            = {literal, NULL, PREC_NONE},
    [TOKEN_FALSE]           = {literal, NULL, PREC_NONE},
    [TOKEN_AND]             = {NULL, and_, PREC_AND},
    [TOKEN_DO]              = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE]            = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSEIF]          = {NULL, NULL, PREC_NONE},
    [TOKEN_END]             = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR]             = {NULL, NULL, PREC_NONE},
    [TOKEN_FUNCTION]        = {anonFunction, NULL, PREC_NONE},
    [TOKEN_CLASS]           = {NULL, NULL, PREC_NONE},
    [TOKEN_IF]              = {NULL, NULL, PREC_NONE},
    [TOKEN_LOCAL]           = {NULL, NULL, PREC_NONE},
    [TOKEN_NOT]             = {NULL, NULL, PREC_NONE},
    [TOKEN_OR]              = {NULL, or_,  PREC_OR},
    [TOKEN_RETURN]          = {NULL, NULL, PREC_NONE},
    [TOKEN_THEN]            = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE]           = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_VAR]             = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]             = {NULL, NULL, PREC_NONE}
};

static ParseRule* getRule(CTokenType type) {
    return &ruleTable[type];
}

static void parsePrecedence(CParseState *pstate, Precedence prec) {
    advance(pstate);

    ParseFunc prefix = getRule(pstate->previous.type)->prefix;

    if (prefix == NULL)
        return;

    bool canAssign = prec <= PREC_ASSIGNMENT;
    prefix(pstate, canAssign);

    while (prec <= getRule(pstate->current.type)->level) {
        ParseFunc infix = getRule(pstate->current.type)->infix;
        advance(pstate);
        infix(pstate, canAssign);
    }

    if (canAssign && match(pstate, TOKEN_EQUAL)) {
        error(pstate, "Invalid assignment!");
    }
}

static void declareLocal(CParseState *pstate, bool forceLocal) {
    if (pstate->compiler->scopeDepth == 0 && !forceLocal)
        return;

    CToken* name = &pstate->previous;

    // check if we already have a local with that identifier
    for (int i = 0; i < pstate->compiler->localCount; i++) {
        Local *local = &pstate->compiler->locals[i];

        // we've reached a previous scope or an invalid scope, stop checking lol
        if (local->depth != -1 && pstate->compiler->scopeDepth > local->depth)
            break;

        if (identifiersEqual(name, &local->name))
            error(pstate, "There's already a local in scope with this name!");
    }

    addLocal(pstate, *name);
}

static uint16_t parseVariable(CParseState *pstate, const char* errorMessage, bool forceLocal) {
    consume(pstate, TOKEN_IDENTIFIER, errorMessage);

    declareLocal(pstate, forceLocal);
    if (pstate->compiler->scopeDepth > 0 || forceLocal)
        return pstate->compiler->localCount - 1;

    return identifierConstant(pstate, &pstate->previous);
}

static void defineVariable(CParseState *pstate, uint16_t global, bool forceLocal) {
    if (pstate->hadError)
        return;

    if (pstate->compiler->scopeDepth > 0 || forceLocal) {
        markInitialized(pstate, global);
        valuePopped(pstate, 1); // the local stays on the stack!
        return;
    }
    
    writeu8(pstate, OP_SETGLOBAL);
    writeu16(pstate, global);

    valuePopped(pstate, 1);
}

static void _class(CParseState *pstate) {
    uint16_t var = parseVariable(pstate, "Expected identifer!", false);
    int entries = 0;
    
    while (!match(pstate, TOKEN_END) && !match(pstate, TOKEN_EOF) && !pstate->hadError) {
        if (match(pstate, TOKEN_FUNCTION)) {
            // define method
            consume(pstate, TOKEN_IDENTIFIER, "Expected identifier!");
            uint16_t fieldIdent = identifierConstant(pstate, &pstate->previous);

            // OP_NEWOBJECT expects the key on the stack before the value
            writeu8(pstate, OP_LOADCONST);
            writeu16(pstate, fieldIdent);

            function(pstate, FTYPE_METHOD);
            valuePopped(pstate, 1);
        }

        entries++;
    }

    writeu8(pstate, OP_NEWOBJECT);
    writeu8(pstate, entries);
    valuePushed(pstate, 1);
    defineVariable(pstate, var, false);
}

static void popLocals(CParseState *pstate, int toScope) {
    if (pstate->hadError)
        return;

    // count the locals in scope to pop
    int localsToPop = 0;

    while (pstate->compiler->localCount > 0 && pstate->compiler->locals[pstate->compiler->localCount - 1].depth > toScope) {
        Local *local = &pstate->compiler->locals[pstate->compiler->localCount - 1];

        if (local->isCaptured) { // local needs to be closed over so other closures can reference it
            // first though, if there are other locals in queue to pop first, go ahead and pop those :)
            if (localsToPop > 0) {
                writePop(pstate, localsToPop);
                localsToPop = 0;
            }

            writeu8(pstate, OP_CLOSE);
        } else {
            localsToPop++;
        }

        pstate->compiler->localCount--;
    }

    if (localsToPop > 0) {
        writePop(pstate, localsToPop);
    }
}

static void beginScope(CParseState *pstate) {
    pstate->compiler->scopeDepth++;
}

static void endScope(CParseState *pstate) {
    pstate->compiler->scopeDepth--;

    popLocals(pstate, pstate->compiler->scopeDepth);
}

// parses expressionStatements until a TOKEN_END is consumed
static void block(CParseState *pstate) {
    while(!check(pstate, TOKEN_END) && !check(pstate, TOKEN_EOF) && !check(pstate, TOKEN_ERROR)) {
        declaration(pstate);
    }

    consume(pstate, TOKEN_END, "'end' expected to end block.'");
}

static void varDeclaration(CParseState *pstate, bool forceLocal) {
    uint16_t global = parseVariable(pstate, "Expected identifer!", forceLocal);

    if (match(pstate, TOKEN_EQUAL)) { // assigning a variable
        valuePopped(pstate, 1); // we are expecting a value

        // consume all the ','
        do {
            expression(pstate);
        } while (match(pstate, TOKEN_COMMA));

        if (pstate->compiler->pushedValues < pstate->compiler->savedPushed) {
            writeu8(pstate, OP_NIL); // didn't get expected result
            valuePushed(pstate, 1);
        }

        valuePushed(pstate, 1);
    } else if (match(pstate, TOKEN_COMMA)) {
        valuePopped(pstate, 1); // we are expecting a value
        varDeclaration(pstate, forceLocal);

        if (pstate->compiler->pushedValues < pstate->compiler->savedPushed) {
            writeu8(pstate, OP_NIL); // didn't get expected result
            valuePushed(pstate, 1);
        }

        valuePushed(pstate, 1); // we already popped, & when we call defineVariable it'll pop, so go ahead and fix it here
    } else {
        writeu8(pstate, OP_NIL);
        valuePushed(pstate, 1);
    }

    defineVariable(pstate, global, forceLocal);
}

static void ifStatement(CParseState *pstate) {
    expression(pstate);
    consume(pstate, TOKEN_THEN, "Expect 'then' after expression.");

    int jump = writeJmp(pstate, OP_PEJMP);
    valuePopped(pstate, 1); // OP_PEJMP pops the conditional!

    // parse until 'end' or 'else'
    beginScope(pstate);
    
    while(!check(pstate, TOKEN_END) && !check(pstate, TOKEN_ELSE) && !check(pstate, TOKEN_ELSEIF) && !check(pstate, TOKEN_EOF) && !check(pstate, TOKEN_ERROR)) {
        declaration(pstate);
    }

    endScope(pstate);

    if (match(pstate, TOKEN_ELSE)) {
        int elseJump = writeJmp(pstate, OP_JMP);
        
        // setup our jump
        patchJmp(pstate, jump);

        // parse until 'end'
        beginScope(pstate);
        block(pstate);
        endScope(pstate);

        patchJmp(pstate, elseJump);
    } else if (match(pstate, TOKEN_ELSEIF)) {
        int elseJump = writeJmp(pstate, OP_JMP);

        // setup our jump
        patchJmp(pstate, jump);

        ifStatement(pstate); // recursively call into ifStatement
        patchJmp(pstate, elseJump);
    } else { // the most vanilla if statement possible (no else, no elseif)
        patchJmp(pstate, jump);
        consume(pstate, TOKEN_END, "'end' expected to end block.");
    }
}

static void whileStatement(CParseState *pstate) {
    int jumpLocation = getChunk(pstate)->count;
    expression(pstate);

    consume(pstate, TOKEN_DO, "expected 'do' after conditional expression.");

    int exitJump = writeJmp(pstate, OP_PEJMP); // pop equality jump
    valuePopped(pstate, 1); // OP_PEJMP pops the conditional!

    beginScope(pstate);
    block(pstate); // parse until 'end'
    endScope(pstate);

    writeJmpBack(pstate, jumpLocation);
    patchJmp(pstate, exitJump);
}

static void function(CParseState *pstate, FunctionType type) {
    CCompilerState compiler;
    initCompilerState(pstate, &compiler, type, pstate->compiler);

    int savedPushed = pstate->compiler->pushedValues;
    // start parsing function
    beginScope(pstate);

    // parse the parameters
    consume(pstate, TOKEN_LEFT_PAREN, "Expected '(' after identifier.");
    if (!check(pstate, TOKEN_RIGHT_PAREN)) {
        do {
            // add arg to function
            compiler.function->args++;
            if (compiler.function->args > UINT16_MAX - 1) { // -1 since the function would already be on the stack
                errorAtCurrent(pstate, "Too many parameters!");
            }

            // parse identifier for param (force them to be a local)
            uint16_t funcIdent = parseVariable(pstate, "Expected identifier for function!", true);
            defineVariable(pstate, funcIdent, true);
            valuePushed(pstate, 1); // they *will* be populated during runtime
        } while (match(pstate, TOKEN_COMMA));
    }
    consume(pstate, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

    // compile function block
    block(pstate);
    alignStack(pstate, savedPushed);
    endScope(pstate);

    CObjFunction *objFunc = endCompiler(pstate);

    // push closure
    writeu8(pstate, OP_CLOSURE);
    writeu16(pstate, makeConstant(pstate, cosmoV_newObj(objFunc)));
    valuePushed(pstate, 1);

    // tell the vm what locals/upvalues to pass to this closure
    for (int i = 0; i < objFunc->upvals; i++) {
        writeu8(pstate, compiler.upvalues[i].isLocal ? OP_GETLOCAL : OP_GETUPVAL);
        writeu8(pstate, compiler.upvalues[i].index);
    }
}

static void functionDeclaration(CParseState *pstate) {
    uint16_t var = parseVariable(pstate, "Expected identifer!", false);

    if (pstate->compiler->scopeDepth > 0)
        markInitialized(pstate, var);

    function(pstate, FTYPE_FUNCTION);

    defineVariable(pstate, var, false);
}

static void returnStatement(CParseState *pstate) {
    if (pstate->compiler->type != FTYPE_FUNCTION) {
        error(pstate, "Expected 'return' in function!");
        return;
    }

    if (blockFollow(pstate->current)) { // does this return have a value
        writeu8(pstate, OP_NIL);
        writeu8(pstate, OP_RETURN);
        return;
    }

    expression(pstate);
    writeu8(pstate, OP_RETURN);
    valuePopped(pstate, 1);
}

static void localFunction(CParseState *pstate) {
    uint16_t var = parseVariable(pstate, "Expected identifer!", true);
    markInitialized(pstate, var);

    function(pstate, FTYPE_FUNCTION);

    defineVariable(pstate, var, true);
}

static void forLoop(CParseState *pstate) {
    beginScope(pstate);

    consume(pstate, TOKEN_LEFT_PAREN, "Expected '(' after 'for'");

    // parse initalizer
    if (!match(pstate, TOKEN_EOS)) {
        expressionStatement(pstate);
        consume(pstate, TOKEN_EOS, "Expected ';' after initalizer!");
    }

    int loopStart = getChunk(pstate)->count;
    
    // parse conditional
    int exitJmp = -1;
    if (!match(pstate, TOKEN_EOS)) {
        expression(pstate);
        consume(pstate, TOKEN_EOS, "Expected ';' after conditional");

        exitJmp = writeJmp(pstate, OP_PEJMP);
        valuePopped(pstate, 1);
    }

    // parse iterator
    if (!match(pstate, TOKEN_RIGHT_PAREN)) {
        int bodyJmp = writeJmp(pstate, OP_JMP);

        int iteratorStart = getChunk(pstate)->count;
        expression(pstate);
        consume(pstate, TOKEN_RIGHT_PAREN, "Expected ')' after iterator");

        writeJmpBack(pstate, loopStart);
        loopStart = iteratorStart;
        patchJmp(pstate, bodyJmp);
    }

    consume(pstate, TOKEN_DO, "Expected 'do'");

    beginScope(pstate); // fixes stack issues
    block(pstate); // parses until 'end'
    endScope(pstate);

    writeJmpBack(pstate, loopStart);

    if (exitJmp != -1) {
        patchJmp(pstate, exitJmp);
    }

    endScope(pstate);
}

static void synchronize(CParseState *pstate) {
    pstate->panic = false;

    while (pstate->current.type != TOKEN_EOF) {
        if (pstate->previous.type == TOKEN_EOS)
            return;

        advance(pstate);
    }
}

static void expression(CParseState *pstate) {
    parsePrecedence(pstate, PREC_ASSIGNMENT);
}

static void expressionStatement(CParseState *pstate) {
    pstate->compiler->savedPushed = pstate->compiler->pushedValues;

    if (match(pstate, TOKEN_VAR)) {
        varDeclaration(pstate, false);
    } else if (match(pstate, TOKEN_LOCAL)) {
        // force declare a local
        if (match(pstate, TOKEN_FUNCTION))
            localFunction(pstate); // force local a function
        else
            varDeclaration(pstate, true); // force local a variable
    } else if (match(pstate, TOKEN_IF)) {
        ifStatement(pstate);
    } else if (match(pstate, TOKEN_DO)) {
        beginScope(pstate);
        block(pstate);
        endScope(pstate);
    } else if (match(pstate, TOKEN_WHILE)) {
        whileStatement(pstate);
    } else if (match(pstate, TOKEN_FOR)) {
        forLoop(pstate);
    } else if (match(pstate, TOKEN_FUNCTION)) {
        functionDeclaration(pstate);
    } else if (match(pstate, TOKEN_CLASS)) {
        _class(pstate);
    } else if (match(pstate, TOKEN_RETURN)) {
        returnStatement(pstate);
    } else {
        expression(pstate);
    }

    // realign the stack
    alignStack(pstate, pstate->compiler->savedPushed);
}

static void statement(CParseState *pstate) {
    expressionStatement(pstate);
}

static void declaration(CParseState *pstate) {
    statement(pstate);

    // if we paniced, skip the whole statement!
    if (pstate->panic)
        synchronize(pstate);
}

static CObjFunction *endCompiler(CParseState *pstate) {
    popLocals(pstate, pstate->compiler->scopeDepth); // remove the locals from other scopes
    writeu8(pstate, OP_NIL);
    writeu8(pstate, OP_RETURN);

    // update pstate to next compiler state
    CCompilerState *cachedCCState = pstate->compiler;
    pstate->compiler = cachedCCState->enclosing;

    return cachedCCState->function;
}

// ================================================================ [API] ================================================================

CObjFunction* cosmoP_compileString(CState *state, const char *source) {
    CParseState parser;
    CCompilerState compiler;
    cosmoM_freezeGC(state); // ignore all GC events while compiling
    initParseState(&parser, &compiler, state, source);

    advance(&parser);

    while (!match(&parser, TOKEN_EOF)) {
        declaration(&parser);
    }

    consume(&parser, TOKEN_EOF, "End of file expected!");

    popLocals(&parser, -1); // needed to close over the values

    if (parser.hadError) { // we don't free the function, the state already has a reference to it in it's linked list of objects!
        endCompiler(&parser);
        freeParseState(&parser);

        // the VM still expects a result on the stack TODO: push the error string to the stack
        cosmoV_pushValue(state, cosmoV_newNil());
        cosmoM_unfreezeGC(state);
        return NULL;
    }

    CObjFunction* resFunc = compiler.function;
    // VM expects the closure on the stack :P (we do this before ending the compiler so our GC doesn't free it)
    cosmoV_pushValue(state, cosmoV_newObj((CObj*)cosmoO_newClosure(state, resFunc)));

    endCompiler(&parser);
    freeParseState(&parser);
    cosmoM_unfreezeGC(state);
    return resFunc;
}