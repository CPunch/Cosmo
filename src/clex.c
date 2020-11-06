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
    {TOKEN_IF, "if", 2},
    {TOKEN_LOCAL, "local", 5},
    {TOKEN_NIL, "nil", 3},
    {TOKEN_NOT, "not", 3},
    {TOKEN_OR, "or", 2},
    {TOKEN_RETURN, "return", 6},
    {TOKEN_THEN, "then", 4},
    {TOKEN_TRUE, "true", 4},
    {TOKEN_VAR, "var", 3},
    {TOKEN_THIS, "this", 4},
    {TOKEN_WHILE, "while", 5}
};


static CToken makeToken(CLexState *state, CTokenType type) {
    CToken token;
    token.type = type;
    token.start = state->startChar;
    token.length = state->currentChar - state->startChar; // delta between start & current
    token.line = state->line;
    
    state->lastType = type;

    return token;
}

static CToken makeError(CLexState *state, const char *msg) {
    CToken token;
    token.type = TOKEN_ERROR;
    token.start = (char*)msg;
    token.length = strlen(msg);
    token.line = state->line;

    return token;
}

static inline bool isEnd(CLexState *state) {
    return state->isEnd;
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
            case ' ':
            case '\r':
            case '\t':
                next(state); // consume the whitespace
                break;
            case '\n': // mark new line, make the main loop consume it
                state->line++;
                return;
            case '-': // consume comments
                if (peekNext(state) == '-') {
                    
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
    while (peek(state) != '"' && !isEnd(state)) {
        if (peek(state) == '\n') // strings can't stretch across lines
            return makeError(state, "Unterminated string!");
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
    state->openedBraces = 0;
    state->isEnd = false;
    state->lastType = TOKEN_ERROR;

    return state;
}

void cosmoL_freeLexState(CState *state, CLexState *lstate) {
    cosmoM_free(state, CLexState, lstate);
}

CToken cosmoL_scanToken(CLexState *state) {
_scanTokenEnter:
    skipWhitespace(state);

    state->startChar = state->currentChar;

    if (isEnd(state))
        return makeToken(state, TOKEN_EOF);
    
    char c = next(state);

    switch (c) {
        // single character tokens
        case '(': state->openedBraces++; return makeToken(state, TOKEN_LEFT_PAREN);
        case ')': state->openedBraces--; return makeToken(state, TOKEN_RIGHT_PAREN);
        case '{': state->openedBraces++; return makeToken(state, TOKEN_LEFT_BRACE);
        case '}': state->openedBraces--; return makeToken(state, TOKEN_RIGHT_BRACE);
        case '[': state->openedBraces++; return makeToken(state, TOKEN_LEFT_BRACKET);
        case ']': state->openedBraces--; return makeToken(state, TOKEN_RIGHT_BRACKET);
        case '\0':
            state->isEnd = true;
            if (state->lastType == TOKEN_EOS)
                return makeToken(state, TOKEN_EOF);
            // fall through
        case ';': return makeToken(state, TOKEN_EOS);
        case ',': return makeToken(state, TOKEN_COMMA);
        case '+': return makeToken(state, TOKEN_PLUS);
        case '-': return makeToken(state, TOKEN_MINUS);
        case '*': return makeToken(state, TOKEN_STAR);
        case '/': return makeToken(state, TOKEN_SLASH);
        case '\n': { // might be treated like a TOKEN_EOS
            if (state->openedBraces == 0 && state->lastType != TOKEN_EOS)
                return makeToken(state, TOKEN_EOS);
            else // go back to the start
                goto _scanTokenEnter;
        }
        // two character tokens
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