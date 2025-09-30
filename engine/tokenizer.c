#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const bool char_is_alnum[256] = {['A' ... 'Z'] = true, ['a' ... 'z'] = true, ['0' ... '9'] = true, ['_'] = true};
static const bool char_is_digit[256] = {['0' ... '9'] = true};
static const bool char_is_whitespace[256] = {[' '] = true, ['\t'] = true, ['\n'] = true, ['\r'] = true, ['\v'] = true, ['\f'] = true};

void tokenizer_init(Tokenizer *tokenizer, const char *input) {
  tokenizer->input = input;
  tokenizer->current = input;
  tokenizer->line = 1;
  tokenizer->column = 1;
}

static Token read_identifier(Tokenizer *tokenizer);
static Token read_number(Tokenizer *tokenizer);
static Token read_string(Tokenizer *tokenizer);
static Token read_raw_string(Tokenizer *tokenizer);
static Token handle_single_char(Tokenizer *tokenizer);
static Token handle_equal(Tokenizer *tokenizer);
static Token handle_bang(Tokenizer *tokenizer);
static Token handle_less(Tokenizer *tokenizer);
static Token handle_greater(Tokenizer *tokenizer);
static Token handle_dot(Tokenizer *tokenizer);
static Token handle_slash(Tokenizer *tokenizer);
static Token handle_comment(Tokenizer *tokenizer);
static Token handle_unknown(Tokenizer *tokenizer);

typedef Token (*TokenHandler)(Tokenizer *);

static const TokenHandler token_handlers[256] = {
    ['A' ... 'Z'] = read_identifier,
    ['a' ... 'z'] = read_identifier,
    ['_'] = read_identifier,
    ['0' ... '9'] = read_number,
    ['"'] = read_string,
    ['\''] = read_string,
    ['`'] = read_raw_string,
    ['('] = handle_single_char,
    [')'] = handle_single_char,
    ['['] = handle_single_char,
    [']'] = handle_single_char,
    ['{'] = handle_single_char,
    ['}'] = handle_single_char,
    ['.'] = handle_dot,
    [' '] = handle_single_char,
    [','] = handle_single_char,
    [';'] = handle_single_char,
    [':'] = handle_single_char,
    ['+'] = handle_single_char,
    ['-'] = handle_single_char,
    ['*'] = handle_single_char,
    ['/'] = handle_slash,
    ['='] = handle_equal,
    ['!'] = handle_bang,
    ['<'] = handle_less,
    ['>'] = handle_greater,
    ['?'] = handle_single_char,
    ['@'] = handle_single_char,
    ['#'] = handle_comment,
    ['$'] = handle_single_char,
    ['~'] = handle_single_char,
    ['|'] = handle_single_char,
    ['%'] = handle_single_char,
};

static inline char peek_char(Tokenizer *tokenizer) { return *tokenizer->current; }

static char next_char(Tokenizer *tokenizer) {
  if (*tokenizer->current == '\0') return '\0';
  char c = *tokenizer->current++;
  if (c == '\n') {
    tokenizer->line++;
    tokenizer->column = 1;
  } else {
    tokenizer->column++;
  }
  return c;
}

static inline void skip_whitespace(Tokenizer *tokenizer) {
  const char *p = tokenizer->current;
  if (!char_is_whitespace[(unsigned char)*p]) return;

  while (char_is_whitespace[(unsigned char)*p]) {
    p++;
  }

  while (tokenizer->current < p) {
    next_char(tokenizer);
  }
}

static inline Token make_token(TokenType type, const char *start, size_t length) {
  Token token;
  token.type = type;
  token.start = start;
  token.length = length;
  size_t copy_len = (length < MAX_TOKEN_VALUE_LENGTH) ? length : MAX_TOKEN_VALUE_LENGTH - 1;
  memcpy(token.value, start, copy_len);
  token.value[copy_len] = '\0';
  return token;
}

Token tokenizer_next_token(Tokenizer *tokenizer) {
  skip_whitespace(tokenizer);
  char c = peek_char(tokenizer);
  if (c == '\0') return make_token(TOKEN_EOF, tokenizer->current, 0);

  TokenHandler handler = token_handlers[(unsigned char)c];
  if (handler) {
    return handler(tokenizer);
  }
  return handle_unknown(tokenizer);
}

static Token read_identifier(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  while (char_is_alnum[(unsigned char)peek_char(tokenizer)]) {
    next_char(tokenizer);
  }
  return make_token(TOKEN_IDENTIFIER, start, tokenizer->current - start);
}

static Token read_number(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;

  while (char_is_digit[(unsigned char)peek_char(tokenizer)]) {
    next_char(tokenizer);
  }

  if (peek_char(tokenizer) == '.') {
    const char *look = tokenizer->current + 1;
    if (char_is_digit[(unsigned char)*look]) {
      next_char(tokenizer); // consume '.'
      while (char_is_digit[(unsigned char)peek_char(tokenizer)]) {
        next_char(tokenizer);
      }
    }
  }

  return make_token(TOKEN_NUMBER, start, tokenizer->current - start);
}

static Token read_string(Tokenizer *tokenizer) {
  Token token;
  token.type = TOKEN_STRING;
  token.start = tokenizer->current;

  char *write_ptr = token.value;

  do {
    const char quote_char = next_char(tokenizer); // opening quote
    for (;;) {
      char c = next_char(tokenizer);
      if (c == '\0' || c == quote_char) break;

      if (write_ptr >= token.value + MAX_TOKEN_VALUE_LENGTH - 1) {
        if (c == '\\') {
          char esc = next_char(tokenizer);
          (void)esc;
        }
        continue;
      }

      if (c == '\\') {
        char esc = next_char(tokenizer);
        switch (esc) {
        case 'n': *write_ptr++ = '\n'; break;
        case 't': *write_ptr++ = '\t'; break;
        case 'r': *write_ptr++ = '\r'; break;
        case '"': *write_ptr++ = '"'; break;
        case '\'': *write_ptr++ = '\''; break;
        case '\\': *write_ptr++ = '\\'; break;
        case '\0': /* unterminated, stop */ goto string_done;
        default: *write_ptr++ = esc; break;
        }
      } else {
        *write_ptr++ = c;
      }
    }

    const char *p = tokenizer->current;
    while (char_is_whitespace[(unsigned char)*p]) p++;
    if (*p != '"' && *p != '\'') {
      break;
    }
    while (tokenizer->current < p) next_char(tokenizer);

  } while (true);

string_done:
  *write_ptr = '\0';
  token.length = (size_t)(write_ptr - token.value);

  return token;
}

static const TokenType single_char_map[256] = {
    ['('] = TOKEN_LEFT_PAREN,
    [')'] = TOKEN_RIGHT_PAREN,
    ['['] = TOKEN_LEFT_BRACKET,
    [']'] = TOKEN_RIGHT_BRACKET,
    ['{'] = TOKEN_LEFT_BRACE,
    ['}'] = TOKEN_RIGHT_BRACE,
    ['.'] = TOKEN_DOT,
    [','] = TOKEN_COMMA,
    [';'] = TOKEN_SEMICOLON,
    [':'] = TOKEN_COLON,
    ['+'] = TOKEN_PLUS,
    ['-'] = TOKEN_MINUS,
    ['*'] = TOKEN_STAR,
    ['/'] = TOKEN_SLASH,
    ['='] = TOKEN_EQUAL,
    ['!'] = TOKEN_BANG,
    ['<'] = TOKEN_LEFT_ANGLE,
    ['>'] = TOKEN_RIGHT_ANGLE,
    ['?'] = TOKEN_QUESTION,
    ['@'] = TOKEN_AT,
    ['#'] = TOKEN_HASH,
    ['$'] = TOKEN_DOLLAR,
    ['~'] = TOKEN_TILDE,
    ['|'] = TOKEN_PIPE,
    ['%'] = TOKEN_MODULO,
};

static Token handle_single_char(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  char c = next_char(tokenizer);
  return make_token(single_char_map[(unsigned char)c], start, 1);
}

static Token handle_equal(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '='
  if (peek_char(tokenizer) == '=') {
    next_char(tokenizer); // consume second '='
    return make_token(TOKEN_EQUAL_EQUAL, start, 2);
  }
  return make_token(TOKEN_EQUAL, start, 1);
}

static Token handle_bang(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '!'
  if (peek_char(tokenizer) == '=') {
    next_char(tokenizer); // consume '='
    return make_token(TOKEN_NOT_EQUAL, start, 2);
  }
  return make_token(TOKEN_BANG, start, 1);
}

static Token handle_less(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '<'
  if (peek_char(tokenizer) == '=') {
    next_char(tokenizer); // consume '='
    return make_token(TOKEN_LESS_EQUAL, start, 2);
  }
  return make_token(TOKEN_LEFT_ANGLE, start, 1);
}

static Token handle_greater(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '>'
  if (peek_char(tokenizer) == '=') {
    next_char(tokenizer); // consume '='
    return make_token(TOKEN_GREATER_EQUAL, start, 2);
  }
  return make_token(TOKEN_RIGHT_ANGLE, start, 1);
}

static Token handle_dot(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '.'
  if (peek_char(tokenizer) == '.') {
    next_char(tokenizer); // consume second '.'
    return make_token(TOKEN_DOT_DOT, start, 2);
  }
  return make_token(TOKEN_DOT, start, 1);
}

static Token read_raw_string(Tokenizer *tokenizer) {
  Token token;
  token.type = TOKEN_STRING;
  token.start = tokenizer->current;

  char *write_ptr = token.value;

  next_char(tokenizer); // Skip opening backtick

  for (;;) {
    char c = peek_char(tokenizer);
    if (c == '\0' || c == '`') break;
    c = next_char(tokenizer);
    if (write_ptr < token.value + MAX_TOKEN_VALUE_LENGTH - 1) {
      *write_ptr++ = c;
    }
  }

  if (peek_char(tokenizer) == '`') {
    next_char(tokenizer); // Skip closing backtick
  }

  *write_ptr = '\0';
  token.length = (size_t)(write_ptr - token.value);

  return token;
}

static Token handle_slash(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer); // consume '/'

  char next = peek_char(tokenizer);
  if (next == '/') {
    // Single-line comment
    while (peek_char(tokenizer) != '\0' && peek_char(tokenizer) != '\n') {
      next_char(tokenizer);
    }
    return tokenizer_next_token(tokenizer);
  }

  if (next == '*') {
    // Multi-line comment
    next_char(tokenizer); // consume '*'
    while (true) {
      char c = next_char(tokenizer);
      if (c == '\0') {
        // Unterminated comment, treat as EOF
        return make_token(TOKEN_EOF, tokenizer->current, 0);
      }
      if (c == '*' && peek_char(tokenizer) == '/') {
        next_char(tokenizer); // consume '/'
        break;
      }
    }
    return tokenizer_next_token(tokenizer);
  }

  return make_token(TOKEN_SLASH, start, 1);
}

static Token handle_comment(Tokenizer *tokenizer) {
  while (peek_char(tokenizer) != '\0' && peek_char(tokenizer) != '\n') {
    next_char(tokenizer);
  }
  return tokenizer_next_token(tokenizer);
}

static Token handle_unknown(Tokenizer *tokenizer) {
  const char *start = tokenizer->current;
  next_char(tokenizer);
  return make_token(TOKEN_UNKNOWN, start, 1);
}

Token tokenizer_peek_token(Tokenizer *tokenizer) {
  Tokenizer saved_state = *tokenizer;
  Token token = tokenizer_next_token(tokenizer);
  *tokenizer = saved_state;
  return token;
}

Token tokenizer_peek_next_token(Tokenizer *tokenizer) {
  Tokenizer saved_state = *tokenizer;
  tokenizer_next_token(tokenizer);
  Token next_token = tokenizer_next_token(tokenizer);
  *tokenizer = saved_state;
  return next_token;
}

bool tokenizer_consume(Tokenizer *tokenizer, TokenType expected_type) {
  if (tokenizer_peek_token(tokenizer).type == expected_type) {
    tokenizer_next_token(tokenizer);
    return true;
  }
  return false;
}

bool tokenizer_consume_value(Tokenizer *tokenizer, TokenType expected_type, const char *expected_value) {
  if (expected_value == NULL) return false;
  Token token = tokenizer_peek_token(tokenizer);
  if (token.type == expected_type && strcmp(token.value, expected_value) == 0) {
    tokenizer_next_token(tokenizer);
    return true;
  }
  return false;
}

void tokenizer_skip_until(Tokenizer *tokenizer, const TokenType *stop_tokens, int num_stop_tokens) {
  while (true) {
    TokenType current_type = tokenizer_peek_token(tokenizer).type;
    if (current_type == TOKEN_EOF) break;
    for (int i = 0; i < num_stop_tokens; i++) {
      if (current_type == stop_tokens[i]) return;
    }
    tokenizer_next_token(tokenizer);
  }
}

const char *token_type_to_string(TokenType type) {
  static const char *strings[] = {
      [TOKEN_EOF] = "EOF",
      [TOKEN_IDENTIFIER] = "IDENTIFIER",
      [TOKEN_STRING] = "STRING",
      [TOKEN_NUMBER] = "NUMBER",
      [TOKEN_UNKNOWN] = "UNKNOWN",
      [TOKEN_LEFT_PAREN] = "(",
      [TOKEN_RIGHT_PAREN] = ")",
      [TOKEN_LEFT_BRACKET] = "[",
      [TOKEN_RIGHT_BRACKET] = "]",
      [TOKEN_LEFT_BRACE] = "{",
      [TOKEN_RIGHT_BRACE] = "}",
      [TOKEN_DOT] = ".",
      [TOKEN_DOT_DOT] = "..",
      [TOKEN_COMMA] = ",",
      [TOKEN_SEMICOLON] = ";",
      [TOKEN_COLON] = ":",
      [TOKEN_PLUS] = "+",
      [TOKEN_MINUS] = "-",
      [TOKEN_STAR] = "*",
      [TOKEN_SLASH] = "/",
      [TOKEN_EQUAL] = "=",
      [TOKEN_BANG] = "!",
      [TOKEN_LEFT_ANGLE] = "<",
      [TOKEN_RIGHT_ANGLE] = ">",
      [TOKEN_QUESTION] = "?",
      [TOKEN_HASH] = "#",
      [TOKEN_DOLLAR] = "$",
      [TOKEN_TILDE] = "~",
      [TOKEN_PIPE] = "|",
      [TOKEN_MODULO] = "%",
      [TOKEN_EQUAL_EQUAL] = "==",
      [TOKEN_NOT_EQUAL] = "!=",
      [TOKEN_LESS_EQUAL] = "<=",
      [TOKEN_GREATER_EQUAL] = ">=",
  };
  if (type >= 0 && type < (int)(sizeof(strings) / sizeof(strings[0])) && strings[type]) {
    return strings[type];
  }
  return "INVALID";
}
