#include "clex.h"
#include "cmem.h"

#include <string.h>

CReservedWord reservedWords[] = {
    {TOKEN_AND, "and", 3},
    {TOKEN_DO, "do", 2},
    {TOKEN_ELSE, "else", 4},
    {TOKEN_ELSEIF, "elseif", 6},
    {TOKEN_END, "end", 3},
    {TOKEN_FALSE, "false", 5},
    {TOKEN_FOR, "for", 3},
    {TOKEN_FUNCTION, "function", 8},
    {TOKEN_CLASS, "class", 5},
    {TOKEN_IF, "if", 2},
    {TOKEN_LOCAL, "local", 5},
    {TOKEN_NIL, "nil", 3},
    {TOKEN_NOT, "not", 3},
    {TOKEN_OR, "or", 2},
    {TOKEN_RETURN, "return", 6},
    {TOKEN_THEN, "then", 4},
    {TOKEN_TRUE, "true", 4},
    {TOKEN_VAR, "var", 3},
    {TOKEN_WHILE, "while", 5}
};

// returns true if current token is a heap allocated buffer
static bool isBuffer(CLexState *state) {
    return state->buffer !=  NULL;
}

// marks the current token as heap allocated & allocates the buffer
static void makeBuffer(CLexState *state) {
    state->buffer = cosmoM_xmalloc(state->cstate, sizeof(char) * 32); // start with a 32 character long buffer
    state->bufCount = 0;
    state->bufCap = 32;
}

static void resetBuffer(CLexState *state) {
    state->buffer = NULL;
    state->bufCount = 0;
    state->bufCap = 0;
}

// cancels the token heap buffer and free's it
static void freeBuffer(CLexState *state) {
    cosmoM_freearray(state->cstate, char, state->buffer, state->bufCap);

    resetBuffer(state);
}

// adds character to buffer
static void appendBuffer(CLexState *state, char c) {
    cosmoM_growarray(state->cstate, char, state->buffer, state->bufCount, state->bufCap);

    state->buffer[state->bufCount++] = c;
}

// saves the current character to the buffer, grows the buffer as needed
static void saveBuffer(CLexState *state) {
    appendBuffer(state, *state->currentChar);
}

// resets the lex state buffer & returns the allocated buffer as a null terminated string
static char *cutBuffer(CLexState *state) {
    // append the null terminator
    appendBuffer(state, '\0');

    // cache buffer info
    char *buf = state->buffer;
    size_t count = state->bufCount;
    size_t cap = state->bufCap;

    // reset lex state buffer!
    resetBuffer(state);

    // shrink the buffer to only use what we need
    return cosmoM_reallocate(state->cstate, buf, cap, count);
}

static CToken makeToken(CLexState *state, CTokenType type) {
    CToken token;
    token.type = type;
    token.line = state->line;

    if (isBuffer(state)) { // is the buffer heap-allocated?
        token.length = state->bufCount;
        token.start = cutBuffer(state);
    } else {
        token.start = state->startChar;
        token.length = state->currentChar - state->startChar; // delta between start & current
    }
    
    state->lastType = type;

    return token;
}

static CToken makeError(CLexState *state, const char *msg) {
    CToken token;
    token.type = TOKEN_ERROR;
    token.start = (char*)msg;
    token.length = strlen(msg);
    token.line = state->line;

    if (isBuffer(state))
        freeBuffer(state);
        
    return token;
}

static inline bool isEnd(CLexState *state) {
    return *state->currentChar == '\0';
}

static inline bool isNumerical(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; // identifiers can have '_'
}

static bool match(CLexState *state, char expected) {
    if (isEnd(state) || *state->currentChar != expected)
        return false;

    // it matched, so increment the currentChar and return true
    state->currentChar++;
    return true;
}

char peek(CLexState *state) {
    return *state->currentChar;
}

static char peekNext(CLexState *state) {
    if (isEnd(state)) 
        return '\0';
    
    return state->currentChar[1];
}

char next(CLexState *state) {
    state->currentChar++;
    return state->currentChar[-1];
}

CTokenType identifierType(CLexState *state) {
    int length = state->currentChar - state->startChar;

    // check against reserved word list
    for (int i = 0; i < sizeof(reservedWords) / sizeof(CReservedWord); i++) {
        // it matches the reserved word
        if (reservedWords[i].len == length && memcmp(state->startChar, reservedWords[i].word, length) == 0) 
            return reservedWords[i].type;
    }

    // else, it's an identifier
    return TOKEN_IDENTIFIER;
}

void skipWhitespace(CLexState *state) {
    while (true) {
        char c = peek(state);
        switch (c) {
            case '\n': // mark new line
                state->line++;
            case ' ':
            case '\r':
            case '\t':
                next(state); // consume the whitespace
                break;
            case '/': // consume comments
                if (peekNext(state) == '/') {
                    
                    // skip to next line (also let \n be consumed on the next iteration to properly handle that)
                    while (!isEnd(state) && peek(state) != '\n' && peek(state) != '\0') // if it's not a newline or null terminator 
                        next(state);
                    
                    break;
                }
                return; // it's a TOKEN_SLASH, let the main body handle that
            default: // it's no longer whitespace, return!
                return;
        }
    }
}

CToken parseString(CLexState *state) {
    makeBuffer(state); // buffer mode
    while (peek(state) != '"' && !isEnd(state)) {
        if (peek(state) == '\n') // strings can't stretch across lines
            return makeError(state, "Unterminated string!");
        saveBuffer(state); // save the character!
        next(state); // consume
    }

    if (isEnd(state))
        return makeError(state, "Unterminated string!");
    
    next(state); // consume closing quote
    return makeToken(state, TOKEN_STRING);
}

CToken parseNumber(CLexState *state) {
    // consume number
    while (isNumerical(peek(state)))
        next(state);
    
    if (peek(state) == '.' && isNumerical(peekNext(state))) {
        next(state); // consume '.'

        // consume number
        while (isNumerical(peek(state)))
            next(state);
    }

    return makeToken(state, TOKEN_NUMBER);
}

CToken parseIdentifier(CLexState *state) {
    // read literal
    while ((isAlpha(peek(state)) || isNumerical(peek(state))) && !isEnd(state)) 
        next(state);
    
    return makeToken(state, identifierType(state)); // is it a reserved word?
}

CLexState *cosmoL_newLexState(CState *cstate, const char *source) {
    CLexState *state = cosmoM_xmalloc(cstate, sizeof(CLexState));
    state->startChar = (char*)source;
    state->currentChar = (char*)source;
    state->line = 1;
    state->lastLine = 0;
    state->lastType = TOKEN_ERROR;
    state->cstate = cstate;

    resetBuffer(state);

    return state;
}

void cosmoL_freeLexState(CState *state, CLexState *lstate) {
    cosmoM_free(state, CLexState, lstate);
}

CToken cosmoL_scanToken(CLexState *state) {
    skipWhitespace(state);

    state->startChar = state->currentChar;

    if (isEnd(state))
        return makeToken(state, TOKEN_EOF);
    
    char c = next(state);

    switch (c) {
        // single character tokens
        case '(': return makeToken(state, TOKEN_LEFT_PAREN);
        case ')': return makeToken(state, TOKEN_RIGHT_PAREN);
        case '{': return makeToken(state, TOKEN_LEFT_BRACE);
        case '}': return makeToken(state, TOKEN_RIGHT_BRACE);
        case '[': return makeToken(state, TOKEN_LEFT_BRACKET);
        case ']': return makeToken(state, TOKEN_RIGHT_BRACKET);
        case ';': return makeToken(state, TOKEN_EOS);
        case ',': return makeToken(state, TOKEN_COMMA);
        case '*': return makeToken(state, TOKEN_STAR);
        case '/': return makeToken(state, TOKEN_SLASH);
        // two character tokens
        case '+': 
            return match(state, '+') ? makeToken(state, TOKEN_PLUS_PLUS) : makeToken(state, TOKEN_PLUS);
        case '-': 
            return match(state, '-') ? makeToken(state, TOKEN_MINUS_MINUS) : makeToken(state, TOKEN_MINUS);
        case '.': 
            return match(state, '.') ? makeToken(state, TOKEN_DOT_DOT) : makeToken(state, TOKEN_DOT);
        case '!':
            return match(state, '=') ? makeToken(state, TOKEN_BANG_EQUAL) : makeToken(state, TOKEN_BANG);
        case '=':
            return match(state, '=') ? makeToken(state, TOKEN_EQUAL_EQUAL) : makeToken(state, TOKEN_EQUAL);
        case '>':
            return match(state, '=') ? makeToken(state, TOKEN_GREATER_EQUAL) : makeToken(state, TOKEN_GREATER);
        case '<':
            return match(state, '=') ? makeToken(state, TOKEN_LESS_EQUAL) : makeToken(state, TOKEN_LESS);
        // literals
        case '"': return parseString(state);
        default:
            if (isNumerical(c))
                return parseNumber(state);
            if (isAlpha(c))
                return parseIdentifier(state);
    }

    return makeError(state, "Unknown symbol!");
}