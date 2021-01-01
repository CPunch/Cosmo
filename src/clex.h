#ifndef CLEX_H
#define CLEX_H

#include "cosmo.h"

typedef enum {
    // single character tokens
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT,
    TOKEN_DOT_DOT,
    TOKEN_DOT_DOT_DOT,
    TOKEN_MINUS,
    TOKEN_MINUS_MINUS,
    TOKEN_PLUS,
    TOKEN_PLUS_PLUS,
    TOKEN_SLASH,
    TOKEN_STAR,
    TOKEN_POUND,
    TOKEN_PERCENT,
    TOKEN_EOS, // end of statement

    // equality operators
    TOKEN_BANG,
    TOKEN_BANG_EQUAL,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,

    // literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING, // token.start is heap allocated and separate from the source string!
    TOKEN_NUMBER,
    TOKEN_NIL,
    TOKEN_TRUE,
    TOKEN_FALSE,

    // keywords & reserved words
    TOKEN_AND,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_DO,
    TOKEN_ELSE,
    TOKEN_ELSEIF,
    TOKEN_END,
    TOKEN_FOR,
    TOKEN_FUNCTION,
    TOKEN_PROTO,
    TOKEN_IF,
    TOKEN_IN,
    TOKEN_LOCAL,
    TOKEN_NOT,
    TOKEN_OR,
    TOKEN_RETURN,
    TOKEN_THEN,
    TOKEN_VAR,
    TOKEN_WHILE,

    TOKEN_ERROR,
    TOKEN_EOF
} CTokenType;

typedef struct {
    CTokenType type;
    const char *word;
    int len;
} CReservedWord;

typedef struct {
    CTokenType type;
    char *start;
    int length;
    int line;
} CToken;

typedef struct {
    char *currentChar;
    char *startChar;
    char *buffer; // if non-NULL & bufCount > 0, token->start & token->length will be set to buffer & bufCount respectively
    size_t bufCount;
    size_t bufCap; 
    int line; // current line
    int lastLine; // line of the previous consumed token
    bool isEnd;
    CTokenType lastType;
    CState *cstate;
} CLexState;

CLexState *cosmoL_newLexState(CState *state, const char *source);
void cosmoL_freeLexState(CState *state, CLexState *lstate);

CToken cosmoL_scanToken(CLexState *state);

#endif