#include "clex.h"

#include "cmem.h"

#include <string.h>

CReservedWord reservedWords[] = {
    {     TOKEN_AND,      "and", 3},
    {   TOKEN_BREAK,    "break", 5},
    {TOKEN_CONTINUE, "continue", 8},
    {      TOKEN_DO,       "do", 2},
    {    TOKEN_ELSE,     "else", 4},
    {  TOKEN_ELSEIF,   "elseif", 6},
    {     TOKEN_END,      "end", 3},
    {   TOKEN_FALSE,    "false", 5},
    {     TOKEN_FOR,      "for", 3},
    {    TOKEN_FUNC,     "func", 4},
    {      TOKEN_IF,       "if", 2},
    {      TOKEN_IN,       "in", 2},
    {   TOKEN_LOCAL,    "local", 5},
    {     TOKEN_NIL,      "nil", 3},
    {     TOKEN_NOT,      "not", 3},
    {      TOKEN_OR,       "or", 2},
    {   TOKEN_PROTO,    "proto", 5},
    {  TOKEN_RETURN,   "return", 6},
    {    TOKEN_THEN,     "then", 4},
    {    TOKEN_TRUE,     "true", 4},
    {     TOKEN_LET,      "let", 3},
    {   TOKEN_WHILE,    "while", 5}
};

// returns true if current token is a heap allocated buffer
static bool isBuffer(CLexState *state)
{
    return state->buffer != NULL;
}

// marks the current token as heap allocated & allocates the buffer
static void makeBuffer(CLexState *state)
{
    state->buffer =
        cosmoM_xmalloc(state->cstate, sizeof(char) * 32); // start with a 32 character long buffer
    state->bufCount = 0;
    state->bufCap = 32;
}

static void resetBuffer(CLexState *state)
{
    state->buffer = NULL;
    state->bufCount = 0;
    state->bufCap = 0;
}

// cancels the token heap buffer and frees it
static void freeBuffer(CLexState *state)
{
    cosmoM_freeArray(state->cstate, char, state->buffer, state->bufCap);

    resetBuffer(state);
}

// adds character to buffer
static void appendBuffer(CLexState *state, char c)
{
    cosmoM_growArray(state->cstate, char, state->buffer, state->bufCount, state->bufCap);

    state->buffer[state->bufCount++] = c;
}

// saves the current character to the buffer, grows the buffer as needed
static void saveBuffer(CLexState *state)
{
    appendBuffer(state, *state->currentChar);
}

// resets the lex state buffer & returns the allocated buffer as a null terminated string
static char *cutBuffer(CLexState *state, int *length)
{
    // append the null terminator
    appendBuffer(state, '\0');

    // cache buffer info
    char *buf = state->buffer;
    size_t count = state->bufCount;
    size_t cap = state->bufCap;

    *length = count - 1;

    // reset lex state buffer!
    resetBuffer(state);

    // shrink the buffer to only use what we need
    return cosmoM_reallocate(state->cstate, buf, cap, count);
}

static CToken makeToken(CLexState *state, CTokenType type)
{
    CToken token;
    token.type = type;
    token.line = state->line;

    if (isBuffer(state)) { // is the buffer heap-allocated?
        token.start = cutBuffer(state, &token.length);
    } else {
        token.start = state->startChar;
        token.length = state->currentChar - state->startChar; // delta between start & current
    }

    state->lastType = type;

    return token;
}

static CToken makeError(CLexState *state, const char *msg)
{
    CToken token;
    token.type = TOKEN_ERROR;
    token.start = (char *)msg;
    token.length = strlen(msg);
    token.line = state->line;

    if (isBuffer(state))
        freeBuffer(state);

    return token;
}

static inline bool isEnd(CLexState *state)
{
    return *state->currentChar == '\0';
}

static inline bool isNumerical(char c)
{
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; // identifiers can have '_'
}

static bool match(CLexState *state, char expected)
{
    if (isEnd(state) || *state->currentChar != expected)
        return false;

    // it matched, so increment the currentChar and return true
    state->currentChar++;
    return true;
}

static char peek(CLexState *state)
{
    return *state->currentChar;
}

static char peekNext(CLexState *state)
{
    if (isEnd(state))
        return '\0';

    return state->currentChar[1];
}

static char next(CLexState *state)
{
    if (isEnd(state))
        return '\0'; // return a null terminator
    state->currentChar++;
    return state->currentChar[-1];
}

static bool isHex(char c)
{
    return isNumerical(c) || ('A' <= c && 'F' >= c) || ('a' <= c && 'f' >= c);
}

static CTokenType identifierType(CLexState *state)
{
    int length = state->currentChar - state->startChar;

    // check against reserved word list
    for (size_t i = 0; i < sizeof(reservedWords) / sizeof(CReservedWord); i++) {
        // it matches the reserved word
        if (reservedWords[i].len == length &&
            memcmp(state->startChar, reservedWords[i].word, length) == 0)
            return reservedWords[i].type;
    }

    // else, it's an identifier
    return TOKEN_IDENTIFIER;
}

static void skipWhitespace(CLexState *state)
{
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
                // skip to next line (also let \n be consumed on the next iteration to properly
                // handle that)
                while (!isEnd(state) &&
                       peek(state) != '\n') // if it's not a newline or the end of the source
                    next(state);

                // keep consuming whitespace
                break;
            } else if (peekNext(state) == '*') { // multiline comments
                while (!isEnd(state) &&
                       !(peek(state) == '*' &&
                         peekNext(state) ==
                             '/')) // if it's the end of the comment or the end of the source
                    next(state);

                // consume the '*/'
                next(state);
                next(state);

                // keep consuming whitespace
                break;
            }
            return; // it's a TOKEN_SLASH, let the main body handle that
        default:    // it's no longer whitespace, return!
            return;
        }
    }
}

static CToken parseString(CLexState *state)
{
    makeBuffer(state); // buffer mode
    while (peek(state) != '"' && !isEnd(state)) {
        switch (peek(state)) {
        case '\n': // strings can't stretch across lines
            return makeError(state, "Unterminated string!");
        case '\\': {     // special character
            next(state); // consume the '\' character

            switch (peek(state)) {
            case 'r':
            case 'n':
                appendBuffer(state, '\n');
                break;
            case 't':
                appendBuffer(state, '\t');
                break;
            case '\\':
                appendBuffer(state, '\\');
                break;
            case '"':
                appendBuffer(state, '"');
                break;
            case 'x':        // hexadecimal character encoding
                next(state); // skip 'x'

                if (isHex(peek(state))) {
                    char *numStart = state->currentChar;

                    // consume the hexnum
                    while (isHex(peek(state)))
                        next(state);
                    state->currentChar--; // since next() is called after

                    unsigned int num = (unsigned int)strtoul(numStart, NULL, 16);

                    if (num > 255) // sanity check
                        return makeError(state, "Character out of range! > 255!");

                    appendBuffer(state, num);
                    break;
                }

                return makeError(state, "Unknown hexadecimal character encoding!");
            case 'b':        // binary character encoding
                next(state); // skip 'b'

                if (peek(state) == '0' || peek(state) == '1') {
                    char *numStart = state->currentChar;

                    // consume the bin
                    while (peek(state) == '0' || peek(state) == '1')
                        next(state);
                    state->currentChar--; // since next() is called after

                    unsigned int num = (unsigned int)strtoul(numStart, NULL, 2);

                    if (num > 255) // sanity check
                        return makeError(state, "Character out of range! > 255!");

                    appendBuffer(state, num);
                    break;
                }

                return makeError(state, "Unknown binary character encoding!");
            default: {
                if (isNumerical(peek(state))) {
                    char *numStart = state->currentChar;

                    // consume the number
                    while (isNumerical(peek(state)))
                        next(state);
                    state->currentChar--; // since next() is called after

                    unsigned int num = (unsigned int)strtoul(numStart, NULL, 10);

                    if (num > 255) // sanity check
                        return makeError(state, "Character out of range! > 255!");

                    appendBuffer(state, num);
                    break;
                }

                return makeError(
                    state, "Unknown special character!"); // TODO: maybe a more descriptive error?
            }
            }

            next(state); // consume special character
            break;
        }
        default: {
            saveBuffer(state); // save the character!
            next(state);       // consume
        }
        }
    }

    if (isEnd(state))
        return makeError(state, "Unterminated string!");

    next(state); // consume closing quote
    return makeToken(state, TOKEN_STRING);
}

static CToken parseNumber(CLexState *state)
{
    switch (peek(state)) {
    case 'x': // hexadecimal number
        next(state);

        while (isHex(peek(state)))
            next(state);

        return makeToken(state, TOKEN_HEXNUMBER);
    case 'b': // binary number
        next(state);

        while (peek(state) == '0' || peek(state) == '1')
            next(state);

        return makeToken(state, TOKEN_BINNUMBER);
    default: // it's a one digit number!!!!!
        if (!isNumerical(peek(state)) && !(peek(state) == '.'))
            return makeToken(state, TOKEN_NUMBER);
        // if it is a number, fall through and parse normally
    }

    // consume number
    while (isNumerical(peek(state))) {
        next(state);
    }

    if (peek(state) == '.' && isNumerical(peekNext(state))) {
        next(state); // consume '.'

        // consume number
        while (isNumerical(peek(state)))
            next(state);
    }

    return makeToken(state, TOKEN_NUMBER);
}

static CToken parseIdentifier(CLexState *state)
{
    // read literal
    while ((isAlpha(peek(state)) || isNumerical(peek(state))) && !isEnd(state))
        next(state);

    return makeToken(state, identifierType(state)); // is it a reserved word?
}

void cosmoL_initLexState(CState *cstate, CLexState *state, const char *source)
{
    state->startChar = (char *)source;
    state->currentChar = (char *)source;
    state->line = 1;
    state->lastLine = 0;
    state->lastType = TOKEN_ERROR;
    state->cstate = cstate;

    resetBuffer(state);
}

void cosmoL_cleanupLexState(CState *state, CLexState *lstate)
{
    // stubbed
}

CToken cosmoL_scanToken(CLexState *state)
{
    skipWhitespace(state);

    state->startChar = state->currentChar;

    if (isEnd(state))
        return makeToken(state, TOKEN_EOF);

    char c = next(state);

    switch (c) {
    // single character tokens
    case '(':
        return makeToken(state, TOKEN_LEFT_PAREN);
    case ')':
        return makeToken(state, TOKEN_RIGHT_PAREN);
    case '{':
        return makeToken(state, TOKEN_LEFT_BRACE);
    case '}':
        return makeToken(state, TOKEN_RIGHT_BRACE);
    case '[':
        return makeToken(state, TOKEN_LEFT_BRACKET);
    case ']':
        return makeToken(state, TOKEN_RIGHT_BRACKET);
    case ';':
        return makeToken(state, TOKEN_EOS);
    case ',':
        return makeToken(state, TOKEN_COMMA);
    case ':':
        return makeToken(state, TOKEN_COLON);
    case '*':
        return makeToken(state, TOKEN_STAR);
    case '%':
        return makeToken(state, TOKEN_PERCENT);
    case '^':
        return makeToken(state, TOKEN_CARROT);
    case '#':
        return makeToken(state, TOKEN_POUND);
    case '/':
        return makeToken(state, TOKEN_SLASH);
    // two character tokens
    case '+':
        return match(state, '+') ? makeToken(state, TOKEN_PLUS_PLUS) : makeToken(state, TOKEN_PLUS);
    case '-':
        return match(state, '-') ? makeToken(state, TOKEN_MINUS_MINUS)
                                 : makeToken(state, TOKEN_MINUS);
    case '.':
        return match(state, '.') ? (match(state, '.') ? makeToken(state, TOKEN_DOT_DOT_DOT)
                                                      : makeToken(state, TOKEN_DOT_DOT))
                                 : makeToken(state, TOKEN_DOT);
    case '!':
        return match(state, '=') ? makeToken(state, TOKEN_BANG_EQUAL)
                                 : makeToken(state, TOKEN_BANG);
    case '=':
        return match(state, '=') ? makeToken(state, TOKEN_EQUAL_EQUAL)
                                 : makeToken(state, TOKEN_EQUAL);
    case '>':
        return match(state, '=') ? makeToken(state, TOKEN_GREATER_EQUAL)
                                 : makeToken(state, TOKEN_GREATER);
    case '<':
        return match(state, '=') ? makeToken(state, TOKEN_LESS_EQUAL)
                                 : makeToken(state, TOKEN_LESS);
    // literals
    case '"':
        return parseString(state);
    default:
        if (isNumerical(c))
            return parseNumber(state);
        if (isAlpha(c))
            return parseIdentifier(state);
    }

    return makeError(state, "Unknown symbol!");
}
