#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MAX_TOKEN_VALUE_LENGTH 1024

typedef enum {
  TOKEN_EOF,
  TOKEN_IDENTIFIER,
  TOKEN_STRING,
  TOKEN_NUMBER,
  TOKEN_UNKNOWN,
  // Punctuation
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_DOT,
  TOKEN_DOT_DOT,
  TOKEN_COMMA,
  TOKEN_SEMICOLON,
  TOKEN_COLON,
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_STAR,
  TOKEN_SLASH,
  TOKEN_EQUAL,
  TOKEN_BANG,
  TOKEN_LEFT_ANGLE,
  TOKEN_RIGHT_ANGLE,
  TOKEN_QUESTION,
  TOKEN_HASH,
  TOKEN_DOLLAR,
  TOKEN_TILDE,
  TOKEN_PIPE,
  TOKEN_MODULO,
  // Comparison operators
  TOKEN_EQUAL_EQUAL,
  TOKEN_NOT_EQUAL,
  TOKEN_LESS_EQUAL,
  TOKEN_GREATER_EQUAL,
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  size_t length;
  char value[MAX_TOKEN_VALUE_LENGTH];
} Token;

typedef struct {
  const char *input;
  const char *current;
  size_t line;
  size_t column;
} Tokenizer;

void tokenizer_init(Tokenizer *tokenizer, const char *input);
Token tokenizer_next_token(Tokenizer *tokenizer);
Token tokenizer_peek_token(Tokenizer *tokenizer);
Token tokenizer_peek_next_token(Tokenizer *tokenizer);
bool tokenizer_consume(Tokenizer *tokenizer, TokenType expected_type);
bool tokenizer_consume_value(Tokenizer *tokenizer, TokenType expected_type, const char *expected_value);
void tokenizer_skip_until(Tokenizer *tokenizer, const TokenType *stop_tokens, int num_stop_tokens);
const char *token_type_to_string(TokenType type);
