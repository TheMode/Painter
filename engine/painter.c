#include "painter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static Expression *parse_expression(Parser *parser);
static Expression *parse_primary(Parser *parser);
static Expression *parse_additive(Parser *parser);
static Expression *parse_multiplicative(Parser *parser);
static Expression *parse_comparison(Parser *parser);
static Expression *parse_coordinate(Parser *parser);
static Expression *parse_function_call(Parser *parser, const char *name);
static Instruction *parse_instruction(Parser *parser);
static BlockPlacement parse_block_placement(Parser *parser);
static ForLoop parse_for_loop(Parser *parser);
static Occurrence parse_occurrence(Parser *parser);
static Occurrence parse_occurrence_from_type(Parser *parser, const char *type_name);
static Occurrence parse_named_occurrence(Parser *parser, const char *name);
static PaletteDefinition parse_palette_definition(Parser *parser);

// Parser initialization
void parser_init(Parser *parser, const char *input) {
  tokenizer_init(&parser->tokenizer, input);
  parser->current_token = tokenizer_next_token(&parser->tokenizer);
  parser->has_error = false;
  parser->error_message[0] = '\0';
}

void parser_error(Parser *parser, const char *message) {
  if (!parser->has_error) {
    parser->has_error = true;
    snprintf(parser->error_message, sizeof(parser->error_message), "Parse error: %s", message);
  }
}

static void advance(Parser *parser) {
  parser->current_token = tokenizer_next_token(&parser->tokenizer);
}

static bool check(Parser *parser, TokenType type) {
  return parser->current_token.type == type;
}

static bool match(Parser *parser, TokenType type) {
  if (check(parser, type)) {
    advance(parser);
    return true;
  }
  return false;
}

static bool expect(Parser *parser, TokenType type, const char *message) {
  if (check(parser, type)) {
    advance(parser);
    return true;
  }
  parser_error(parser, message);
  return false;
}

// Expression parsing
static Expression *parse_primary(Parser *parser) {
  if (match(parser, TOKEN_AT)) {
    if (!check(parser, TOKEN_IDENTIFIER)) {
      parser_error(parser, "Expected identifier after '@'");
      return NULL;
    }

    char function_name[MAX_TOKEN_VALUE_LENGTH];
    strncpy(function_name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    function_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    advance(parser);

    if (!match(parser, TOKEN_LEFT_PAREN)) {
      parser_error(parser, "Expected '(' after function name");
      return NULL;
    }

    return parse_function_call(parser, function_name);
  }

  if (check(parser, TOKEN_NUMBER)) {
    Expression *expr = malloc(sizeof(Expression));
    if (!expr) {
      parser_error(parser, "Out of memory");
      return NULL;
    }
    expr->type = EXPR_NUMBER;
    expr->number = atof(parser->current_token.value);
    advance(parser);
    return expr;
  }

  if (check(parser, TOKEN_IDENTIFIER)) {
    char identifier[MAX_TOKEN_VALUE_LENGTH];
    strncpy(identifier, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    identifier[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    advance(parser);

    if (match(parser, TOKEN_LEFT_PAREN)) {
      return parse_function_call(parser, identifier);
    }

    Expression *expr = malloc(sizeof(Expression));
    if (!expr) {
      parser_error(parser, "Out of memory");
      return NULL;
    }
    expr->type = EXPR_IDENTIFIER;
    strncpy(expr->identifier, identifier, MAX_TOKEN_VALUE_LENGTH - 1);
    expr->identifier[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    return expr;
  }

  if (match(parser, TOKEN_LEFT_BRACKET)) {
    return parse_coordinate(parser);
  }

  if (match(parser, TOKEN_LEFT_PAREN)) {
    Expression *inner = parse_expression(parser);
    expect(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return inner;
  }

  parser_error(parser, "Expected expression");
  return NULL;
}

static Expression *parse_multiplicative(Parser *parser) {
  Expression *left = parse_primary(parser);
  if (!left) return NULL;

  while (check(parser, TOKEN_STAR) || check(parser, TOKEN_SLASH) || check(parser, TOKEN_MODULO)) {
    BinaryOperator op;
    if (match(parser, TOKEN_STAR)) op = OP_MULTIPLY;
    else if (match(parser, TOKEN_SLASH)) op = OP_DIVIDE;
    else if (match(parser, TOKEN_MODULO)) op = OP_MODULO;
    else break;

    Expression *right = parse_primary(parser);
    if (!right) {
      expression_free(left);
      return NULL;
    }

    Expression *binary = malloc(sizeof(Expression));
    binary->type = EXPR_BINARY_OP;
    binary->binary.op = op;
    binary->binary.left = left;
    binary->binary.right = right;
    left = binary;
  }

  return left;
}

static Expression *parse_additive(Parser *parser) {
  Expression *left = parse_multiplicative(parser);
  if (!left) return NULL;

  while (check(parser, TOKEN_PLUS) || check(parser, TOKEN_MINUS)) {
    BinaryOperator op = match(parser, TOKEN_PLUS) ? OP_ADD : OP_SUBTRACT;
    if (op != OP_ADD) advance(parser); // consume MINUS

    Expression *right = parse_multiplicative(parser);
    if (!right) {
      expression_free(left);
      return NULL;
    }

    Expression *binary = malloc(sizeof(Expression));
    binary->type = EXPR_BINARY_OP;
    binary->binary.op = op;
    binary->binary.left = left;
    binary->binary.right = right;
    left = binary;
  }

  return left;
}

static Expression *parse_comparison(Parser *parser) {
  Expression *left = parse_additive(parser);
  if (!left) return NULL;

  if (check(parser, TOKEN_EQUAL_EQUAL) || check(parser, TOKEN_NOT_EQUAL) ||
      check(parser, TOKEN_LEFT_ANGLE) || check(parser, TOKEN_LESS_EQUAL) ||
      check(parser, TOKEN_RIGHT_ANGLE) || check(parser, TOKEN_GREATER_EQUAL)) {
    
    BinaryOperator op;
    if (match(parser, TOKEN_EQUAL_EQUAL)) op = OP_EQUAL;
    else if (match(parser, TOKEN_NOT_EQUAL)) op = OP_NOT_EQUAL;
    else if (match(parser, TOKEN_LEFT_ANGLE)) op = OP_LESS;
    else if (match(parser, TOKEN_LESS_EQUAL)) op = OP_LESS_EQUAL;
    else if (match(parser, TOKEN_RIGHT_ANGLE)) op = OP_GREATER;
    else if (match(parser, TOKEN_GREATER_EQUAL)) op = OP_GREATER_EQUAL;
    else return left;

    Expression *right = parse_additive(parser);
    if (!right) {
      expression_free(left);
      return NULL;
    }

    Expression *binary = malloc(sizeof(Expression));
    binary->type = EXPR_BINARY_OP;
    binary->binary.op = op;
    binary->binary.left = left;
    binary->binary.right = right;
    return binary;
  }

  return left;
}

static Expression *parse_expression(Parser *parser) {
  return parse_comparison(parser);
}

static Expression *parse_coordinate(Parser *parser) {
  // Expects [ is already consumed
  Expression *coord = malloc(sizeof(Expression));
  coord->type = EXPR_COORDINATE;
  
  // Parse first value (always x)
  coord->coordinate.x = parse_expression(parser);
  if (!coord->coordinate.x) {
    free(coord);
    return NULL;
  }

  // Handle optional comma after first expression (allow both "[x z]" and "[x, z]")
  match(parser, TOKEN_COMMA);

  coord->coordinate.y = NULL;
  coord->coordinate.z = NULL;

  if (!check(parser, TOKEN_RIGHT_BRACKET)) {
    // Parse second value (could be y or z depending on whether there's a third)
    Expression *second = parse_expression(parser);
    if (!second) {
      expression_free(coord->coordinate.x);
      free(coord);
      return NULL;
    }

    // Optional comma between second and third values
    match(parser, TOKEN_COMMA);

    if (!check(parser, TOKEN_RIGHT_BRACKET)) {
      // Three values provided: [x y z]
      coord->coordinate.y = second;
      coord->coordinate.z = parse_expression(parser);
      if (!coord->coordinate.z) {
        expression_free(coord->coordinate.x);
        expression_free(second);
        free(coord);
        return NULL;
      }
    } else {
      // Two values provided: [x z], y defaults to 0
      coord->coordinate.z = second;
    }
  }

  if (!expect(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after coordinate")) {
    expression_free(coord->coordinate.x);
    if (coord->coordinate.y) expression_free(coord->coordinate.y);
    expression_free(coord->coordinate.z);
    free(coord);
    return NULL;
  }

  return coord;
}

static Expression *parse_function_call(Parser *parser, const char *name) {
  Expression *expr = malloc(sizeof(Expression));
  if (!expr) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  expr->type = EXPR_FUNCTION_CALL;
  strncpy(expr->function_call.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  expr->function_call.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  expr->function_call.arg_count = 0;
  expr->function_call.args = NULL;

  int capacity = 0;
  if (!check(parser, TOKEN_RIGHT_PAREN) && !check(parser, TOKEN_EOF)) {
    capacity = 4;
    expr->function_call.args = malloc(sizeof(Expression *) * capacity);
    if (!expr->function_call.args) {
      parser_error(parser, "Out of memory");
      free(expr);
      return NULL;
    }

    while (true) {
      Expression *arg = parse_expression(parser);
      if (!arg) {
        expression_free(expr);
        return NULL;
      }

      if (expr->function_call.arg_count >= capacity) {
        capacity *= 2;
        Expression **resized = realloc(expr->function_call.args, sizeof(Expression *) * capacity);
        if (!resized) {
          parser_error(parser, "Out of memory");
          expression_free(expr);
          return NULL;
        }
        expr->function_call.args = resized;
      }

      expr->function_call.args[expr->function_call.arg_count++] = arg;

      if (!match(parser, TOKEN_COMMA)) {
        break;
      }
    }
  }

  if (!expect(parser, TOKEN_RIGHT_PAREN, "Expected ')' after function arguments")) {
    expression_free(expr);
    return NULL;
  }

  return expr;
}

// Instruction parsing
static BlockPlacement parse_block_placement(Parser *parser) {
  BlockPlacement placement = {0};
  
  // Consume the opening [
  if (!expect(parser, TOKEN_LEFT_BRACKET, "Expected '[' for coordinate")) {
    return placement;
  }
  
  placement.coordinate = parse_coordinate(parser);
  if (!placement.coordinate) return placement;

  // Parse block name (optionally with namespace, e.g., "minecraft:grass_block")
  if (!check(parser, TOKEN_IDENTIFIER)) {
    parser_error(parser, "Expected block name");
    return placement;
  }

  strncpy(placement.block_name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser);

  // Check for namespace separator (:)
  if (match(parser, TOKEN_COLON)) {
    if (!check(parser, TOKEN_IDENTIFIER)) {
      parser_error(parser, "Expected block name after ':'");
      return placement;
    }
    // Append the colon and the rest of the name
    size_t current_len = strlen(placement.block_name);
    if (current_len < MAX_TOKEN_VALUE_LENGTH - 1) {
      placement.block_name[current_len] = ':';
      strncpy(placement.block_name + current_len + 1, 
              parser->current_token.value, 
              MAX_TOKEN_VALUE_LENGTH - current_len - 2);
      placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    }
    advance(parser);
  }

  // Parse optional block properties [key=value,...]
  placement.block_properties[0] = '\0';
  if (check(parser, TOKEN_LEFT_BRACKET)) {
    Token peek = tokenizer_peek_token(&parser->tokenizer);
    if (peek.type == TOKEN_IDENTIFIER || peek.type == TOKEN_RIGHT_BRACKET) {
      advance(parser); // consume '['

      size_t offset = 0;
      while (!check(parser, TOKEN_RIGHT_BRACKET) && !check(parser, TOKEN_EOF)) {
        const char *token_str = parser->current_token.value;
        size_t token_len = strlen(token_str);

        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          strcpy(placement.block_properties + offset, token_str);
          offset += token_len;
        }
        advance(parser);
      }
      expect(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after block properties");
    }
  }

  return placement;
}

static ForLoop parse_for_loop(Parser *parser) {
  ForLoop loop = {0};
  
  advance(parser); // consume 'for'
  
  if (!check(parser, TOKEN_IDENTIFIER)) {
    parser_error(parser, "Expected loop variable name");
    return loop;
  }

  strncpy(loop.variable, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  loop.variable[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser);

  if (parser->current_token.type != TOKEN_IDENTIFIER ||
      strcmp(parser->current_token.value, "in") != 0) {
    parser_error(parser, "Expected 'in' after loop variable");
    return loop;
  }
  advance(parser); // consume 'in'

  loop.start = parse_expression(parser);
  if (!loop.start) return loop;

  expect(parser, TOKEN_DOT_DOT, "Expected '..' in range");

  loop.end = parse_expression(parser);
  if (!loop.end) {
    expression_free(loop.start);
    return loop;
  }

  expect(parser, TOKEN_LEFT_BRACE, "Expected '{' after for loop header");

  // Parse body instructions
  int capacity = 8;
  loop.body = malloc(sizeof(Instruction *) * capacity);
  loop.body_count = 0;

  while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (loop.body_count >= capacity) {
        capacity *= 2;
        loop.body = realloc(loop.body, sizeof(Instruction *) * capacity);
      }
      loop.body[loop.body_count++] = instr;
    }
  }

  expect(parser, TOKEN_RIGHT_BRACE, "Expected '}' after for loop body");

  return loop;
}

static Occurrence parse_occurrence(Parser *parser) {
  Occurrence occurrence = {0};

  advance(parser); // consume '@'

  if (!check(parser, TOKEN_IDENTIFIER)) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  char type_name[MAX_TOKEN_VALUE_LENGTH];
  strncpy(type_name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  type_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser);

  return parse_occurrence_from_type(parser, type_name);
}

static Occurrence parse_named_occurrence(Parser *parser, const char *name) {
  return parse_occurrence_from_type(parser, name);
}

static Occurrence parse_occurrence_from_type(Parser *parser, const char *type_name) {
  Occurrence occurrence = {0};

  strncpy(occurrence.type, type_name, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.type[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';

  occurrence.args = NULL;
  occurrence.arg_count = 0;

  if (match(parser, TOKEN_LEFT_PAREN)) {
    int capacity = 4;
    occurrence.args = malloc(sizeof(Expression *) * capacity);
    if (!occurrence.args) {
      parser_error(parser, "Out of memory");
      return occurrence;
    }

    while (!check(parser, TOKEN_RIGHT_PAREN) && !check(parser, TOKEN_EOF)) {
      Expression *arg = parse_expression(parser);
      if (!arg) {
        return occurrence;
      }

      if (occurrence.arg_count >= capacity) {
        capacity *= 2;
        Expression **resized = realloc(occurrence.args, sizeof(Expression *) * capacity);
        if (!resized) {
          parser_error(parser, "Out of memory");
          return occurrence;
        }
        occurrence.args = resized;
      }

      occurrence.args[occurrence.arg_count++] = arg;

      if (!match(parser, TOKEN_COMMA)) {
        break;
      }
    }

    expect(parser, TOKEN_RIGHT_PAREN, "Expected ')' after occurrence arguments");
  }

  occurrence.condition = NULL;
  if (check(parser, TOKEN_RIGHT_ANGLE) || check(parser, TOKEN_LEFT_ANGLE) ||
      check(parser, TOKEN_EQUAL_EQUAL) || check(parser, TOKEN_NOT_EQUAL) ||
      check(parser, TOKEN_GREATER_EQUAL) || check(parser, TOKEN_LESS_EQUAL)) {
    occurrence.condition = parse_expression(parser);
  }

  expect(parser, TOKEN_LEFT_BRACE, "Expected '{' after occurrence header");

  int body_capacity = 8;
  occurrence.body = malloc(sizeof(Instruction *) * body_capacity);
  occurrence.body_count = 0;

  if (!occurrence.body) {
    parser_error(parser, "Out of memory");
    return occurrence;
  }

  while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
    if (occurrence.body_count >= body_capacity) {
      body_capacity *= 2;
      Instruction **resized = realloc(occurrence.body, sizeof(Instruction *) * body_capacity);
      if (!resized) {
        parser_error(parser, "Out of memory");
        return occurrence;
      }
      occurrence.body = resized;
    }
      occurrence.body[occurrence.body_count++] = instr;
    }
  }

  expect(parser, TOKEN_RIGHT_BRACE, "Expected '}' after occurrence body");

  return occurrence;
}

static PaletteDefinition parse_palette_definition(Parser *parser) {
  PaletteDefinition palette = {0};
  
  // Name is already parsed in assignment
  expect(parser, TOKEN_LEFT_BRACE, "Expected '{' for palette definition");

  int capacity = 16;
  palette.entries = malloc(sizeof(PaletteEntry) * capacity);
  palette.entry_count = 0;

  while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    // Parse key
    if (!check(parser, TOKEN_NUMBER)) {
      parser_error(parser, "Expected number for palette key");
      break;
    }

    PaletteEntry entry = {0};
    entry.key = atoi(parser->current_token.value);
    advance(parser);

    expect(parser, TOKEN_COLON, "Expected ':' after palette key");

    // Parse block name
    if (!check(parser, TOKEN_IDENTIFIER)) {
      parser_error(parser, "Expected block name");
      break;
    }

    strncpy(entry.block_name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    entry.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    advance(parser);

    // Parse optional properties
    entry.block_properties[0] = '\0';
    if (match(parser, TOKEN_LEFT_BRACKET)) {
      size_t offset = 0;
      while (!check(parser, TOKEN_RIGHT_BRACKET) && !check(parser, TOKEN_EOF)) {
        const char *token_str = parser->current_token.value;
        size_t token_len = strlen(token_str);
        
        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          strcpy(entry.block_properties + offset, token_str);
          offset += token_len;
        }
        advance(parser);
      }
      expect(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after block properties");
    }

    if (palette.entry_count >= capacity) {
      capacity *= 2;
      palette.entries = realloc(palette.entries, sizeof(PaletteEntry) * capacity);
    }
    palette.entries[palette.entry_count++] = entry;

    // Optional comma or newline
    match(parser, TOKEN_COMMA);
  }

  expect(parser, TOKEN_RIGHT_BRACE, "Expected '}' after palette definition");

  return palette;
}

static Instruction *parse_instruction(Parser *parser) {
  if (check(parser, TOKEN_EOF)) return NULL;

  Instruction *instr = malloc(sizeof(Instruction));
  if (!instr) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  // Block placement: [x y z] block_name
  if (check(parser, TOKEN_LEFT_BRACKET)) {
    instr->type = INSTR_BLOCK_PLACEMENT;
    instr->block_placement = parse_block_placement(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // For loop: for var in start..end { ... }
  if (check(parser, TOKEN_IDENTIFIER) && 
      strcmp(parser->current_token.value, "for") == 0) {
    instr->type = INSTR_FOR_LOOP;
    instr->for_loop = parse_for_loop(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // Occurrence: @type(...) { ... } or variable = @type(...)
  if (check(parser, TOKEN_AT)) {
    instr->type = INSTR_OCCURRENCE;
    instr->occurrence = parse_occurrence(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // Assignment or palette definition: name = value or name = { ... }
  if (check(parser, TOKEN_IDENTIFIER)) {
    char name[MAX_TOKEN_VALUE_LENGTH];
    strncpy(name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';

    Token next = tokenizer_peek_token(&parser->tokenizer);
    if (next.type == TOKEN_EQUAL) {
      advance(parser); // consume identifier
      advance(parser); // consume '='

      // Check if this is a palette definition or occurrence assignment
      if (check(parser, TOKEN_LEFT_BRACE)) {
        instr->type = INSTR_PALETTE_DEFINITION;
        strncpy(instr->palette_definition.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
        instr->palette_definition.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
        instr->palette_definition = parse_palette_definition(parser);
        strncpy(instr->palette_definition.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
        if (parser->has_error) {
          free(instr);
          return NULL;
        }
        return instr;
      } else if (check(parser, TOKEN_AT)) {
        instr->type = INSTR_OCCURRENCE;
        instr->occurrence = parse_occurrence(parser);
        if (parser->has_error) {
          free(instr);
          return NULL;
        }
        return instr;
      } else {
        instr->type = INSTR_ASSIGNMENT;
        strncpy(instr->assignment.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
        instr->assignment.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
        instr->assignment.value = parse_expression(parser);
        if (parser->has_error) {
          free(instr);
          return NULL;
        }
        return instr;
      }
    } else if (next.type == TOKEN_LEFT_BRACE) {
      advance(parser); // consume identifier
      instr->type = INSTR_OCCURRENCE;
      instr->occurrence = parse_named_occurrence(parser, name);
      if (parser->has_error) {
        free(instr);
        return NULL;
      }
      return instr;
    }
  }

  parser_error(parser, "Unexpected token");
  free(instr);
  return NULL;
}

Program *parse_program(Parser *parser) {
  Program *program = malloc(sizeof(Program));
  if (!program) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  program->instruction_capacity = 32;
  program->instruction_count = 0;
  program->instructions = malloc(sizeof(Instruction *) * program->instruction_capacity);

  while (!check(parser, TOKEN_EOF) && !parser->has_error) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (program->instruction_count >= program->instruction_capacity) {
        program->instruction_capacity *= 2;
        program->instructions = realloc(program->instructions, 
                                       sizeof(Instruction *) * program->instruction_capacity);
      }
      program->instructions[program->instruction_count++] = instr;
    } else if (parser->has_error) {
      break;
    }
  }

  if (parser->has_error) {
    fprintf(stderr, "%s\n", parser->error_message);
  }

  return program;
}

// Memory cleanup
void expression_free(Expression *expr) {
  if (!expr) return;

  switch (expr->type) {
    case EXPR_BINARY_OP:
      expression_free(expr->binary.left);
      expression_free(expr->binary.right);
      break;
    case EXPR_COORDINATE:
      expression_free(expr->coordinate.x);
      expression_free(expr->coordinate.y);
      expression_free(expr->coordinate.z);
      break;
    case EXPR_FUNCTION_CALL:
      for (int i = 0; i < expr->function_call.arg_count; i++) {
        expression_free(expr->function_call.args[i]);
      }
      free(expr->function_call.args);
      break;
    default:
      break;
  }

  free(expr);
}

void instruction_free(Instruction *instr) {
  if (!instr) return;

  switch (instr->type) {
    case INSTR_BLOCK_PLACEMENT:
      expression_free(instr->block_placement.coordinate);
      break;
    case INSTR_ASSIGNMENT:
      expression_free(instr->assignment.value);
      break;
    case INSTR_FOR_LOOP:
      expression_free(instr->for_loop.start);
      expression_free(instr->for_loop.end);
      for (int i = 0; i < instr->for_loop.body_count; i++) {
        instruction_free(instr->for_loop.body[i]);
      }
      free(instr->for_loop.body);
      break;
    case INSTR_OCCURRENCE:
      for (int i = 0; i < instr->occurrence.arg_count; i++) {
        expression_free(instr->occurrence.args[i]);
      }
      free(instr->occurrence.args);
      expression_free(instr->occurrence.condition);
      for (int i = 0; i < instr->occurrence.body_count; i++) {
        instruction_free(instr->occurrence.body[i]);
      }
      free(instr->occurrence.body);
      break;
    case INSTR_PALETTE_DEFINITION:
      free(instr->palette_definition.entries);
      break;
  }

  free(instr);
}

void program_free(Program *program) {
  if (!program) return;

  for (int i = 0; i < program->instruction_count; i++) {
    instruction_free(program->instructions[i]);
  }
  free(program->instructions);
  free(program);
}

// Helper function to calculate bits per entry based on palette size
static int calculate_bits_per_entry(int palette_size) {
  if (palette_size <= 1) return 0;
  
  // Calculate the minimum number of bits needed to represent (palette_size - 1)
  // This is equivalent to floor(log2(palette_size - 1)) + 1
  int bits = 0;
  int temp = palette_size - 1;
  while (temp > 0) {
    bits++;
    temp >>= 1;
  }
  return bits;
}

// Helper function to find or add a block to the palette
static int get_palette_index(char ***palette, int *palette_size, int *palette_capacity, const char *block_string) {
  // Search for existing block in palette
  for (int i = 0; i < *palette_size; i++) {
    if (strcmp((*palette)[i], block_string) == 0) {
      return i;
    }
  }
  
  // Add new block to palette
  if (*palette_size >= *palette_capacity) {
    *palette_capacity *= 2;
    *palette = realloc(*palette, sizeof(char *) * (*palette_capacity));
  }
  
  (*palette)[*palette_size] = malloc(strlen(block_string) + 1);
  strcpy((*palette)[*palette_size], block_string);
  return (*palette_size)++;
}

// Helper function to create a full block string (name + properties)
static void create_block_string(char *buffer, size_t buffer_size, const char *block_name, const char *block_properties) {
  if (block_properties && block_properties[0] != '\0') {
    snprintf(buffer, buffer_size, "%s[%s]", block_name, block_properties);
  } else {
    snprintf(buffer, buffer_size, "%s", block_name);
  }
}

// Variable context for tracking values during execution
typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  double value;
} Variable;

typedef struct {
  Variable *variables;
  int variable_count;
  int variable_capacity;
} VariableContext;

static void context_init(VariableContext *ctx) {
  ctx->variable_capacity = 16;
  ctx->variable_count = 0;
  ctx->variables = malloc(sizeof(Variable) * ctx->variable_capacity);
}

static void context_free(VariableContext *ctx) {
  free(ctx->variables);
}

static void context_set(VariableContext *ctx, const char *name, double value) {
  // Check if variable already exists
  for (int i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      ctx->variables[i].value = value;
      return;
    }
  }
  
  // Add new variable
  if (ctx->variable_count >= ctx->variable_capacity) {
    ctx->variable_capacity *= 2;
    ctx->variables = realloc(ctx->variables, sizeof(Variable) * ctx->variable_capacity);
  }
  
  strncpy(ctx->variables[ctx->variable_count].name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  ctx->variables[ctx->variable_count].name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  ctx->variables[ctx->variable_count].value = value;
  ctx->variable_count++;
}

static double context_get(VariableContext *ctx, const char *name) {
  for (int i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      return ctx->variables[i].value;
    }
  }
  return 0.0;
}

// Helper function to evaluate an expression to a number
static double evaluate_expression(Expression *expr, VariableContext *ctx) {
  if (!expr) return 0.0;
  
  switch (expr->type) {
    case EXPR_NUMBER:
      return expr->number;
    case EXPR_IDENTIFIER:
      return context_get(ctx, expr->identifier);
    case EXPR_BINARY_OP: {
      double left = evaluate_expression(expr->binary.left, ctx);
      double right = evaluate_expression(expr->binary.right, ctx);
      switch (expr->binary.op) {
        case OP_ADD: return left + right;
        case OP_SUBTRACT: return left - right;
        case OP_MULTIPLY: return left * right;
        case OP_DIVIDE: return right != 0 ? left / right : 0;
        case OP_MODULO: return (int)left % (int)right;
        default: return 0.0;
      }
    }
    default:
      return 0.0;
  }
}

// Forward declaration for recursive processing
static void process_instruction(Instruction *instr, VariableContext *ctx, 
                                int base_x, int base_y, int base_z,
                                int *block_indices, char ***palette, 
                                int *palette_size, int *palette_capacity);

// Process a single instruction and its effects on the section
static void process_instruction(Instruction *instr, VariableContext *ctx,
                                int base_x, int base_y, int base_z,
                                int *block_indices, char ***palette,
                                int *palette_size, int *palette_capacity) {
  if (!instr) return;
  
  switch (instr->type) {
    case INSTR_BLOCK_PLACEMENT: {
      BlockPlacement *placement = &instr->block_placement;
      
      if (placement->coordinate && placement->coordinate->type == EXPR_COORDINATE) {
        // Evaluate coordinates
        int x = (int)evaluate_expression(placement->coordinate->coordinate.x, ctx);
        int y = placement->coordinate->coordinate.y ? 
                (int)evaluate_expression(placement->coordinate->coordinate.y, ctx) : 0;
        int z = (int)evaluate_expression(placement->coordinate->coordinate.z, ctx);
        
        // Check if block is within this section
        if (x >= base_x && x < base_x + 16 &&
            y >= base_y && y < base_y + 16 &&
            z >= base_z && z < base_z + 16) {
          
          // Convert to section-local coordinates
          int local_x = x - base_x;
          int local_y = y - base_y;
          int local_z = z - base_z;
          
          // Calculate index in block array
          // Standard Minecraft format: y*256 + z*16 + x (array[y][z][x])
          int block_index = local_y * 256 + local_z * 16 + local_x;
          
          // Create full block string
          char block_string[MAX_TOKEN_VALUE_LENGTH * 2];
          create_block_string(block_string, sizeof(block_string), 
                            placement->block_name, placement->block_properties);
          
          // Get or add to palette
          int palette_index = get_palette_index(palette, palette_size, 
                                               palette_capacity, block_string);
          
          block_indices[block_index] = palette_index;
        }
      }
      break;
    }
    
    case INSTR_ASSIGNMENT: {
      Assignment *assignment = &instr->assignment;
      double value = evaluate_expression(assignment->value, ctx);
      context_set(ctx, assignment->name, value);
      break;
    }
    
    case INSTR_FOR_LOOP: {
      ForLoop *loop = &instr->for_loop;
      
      // Evaluate loop bounds
      int start = (int)evaluate_expression(loop->start, ctx);
      int end = (int)evaluate_expression(loop->end, ctx);
      
      // Execute loop body for each iteration
      for (int i = start; i < end; i++) {
        // Set loop variable
        context_set(ctx, loop->variable, (double)i);
        
        // Process all body instructions
        for (int j = 0; j < loop->body_count; j++) {
          process_instruction(loop->body[j], ctx, base_x, base_y, base_z,
                            block_indices, palette, palette_size, palette_capacity);
        }
      }
      break;
    }
    
    case INSTR_OCCURRENCE:
      // TODO: Implement occurrence handling
      break;
    
    case INSTR_PALETTE_DEFINITION:
      // TODO: Implement palette definition handling
      break;
  }
}

// Section generation function
Section *generate_section(Program *program, int section_x, int section_y, int section_z) {
  if (!program) return NULL;
  
  Section *section = malloc(sizeof(Section));
  if (!section) return NULL;
  
  // Initialize palette
  int palette_capacity = 16;
  section->palette = malloc(sizeof(char *) * palette_capacity);
  section->palette_size = 0;
  
  // Initialize block array (16x16x16 = 4096 blocks)
  // Using air as default (index 0)
  int block_indices[4096] = {0};
  
  // Add air as the first palette entry
  get_palette_index(&section->palette, &section->palette_size, &palette_capacity, "minecraft:air");
  
  // Convert section coordinates to world coordinates
  int base_x = section_x * 16;
  int base_y = section_y * 16;
  int base_z = section_z * 16;
  
  // Initialize variable context
  VariableContext ctx;
  context_init(&ctx);
  
  // Process all instructions
  for (int i = 0; i < program->instruction_count; i++) {
    process_instruction(program->instructions[i], &ctx, base_x, base_y, base_z,
                       block_indices, &section->palette, 
                       &section->palette_size, &palette_capacity);
  }
  
  // Clean up context
  context_free(&ctx);
  
  // Calculate bits per entry
  section->bits_per_entry = calculate_bits_per_entry(section->palette_size);
  
  // Pack block indices into data array
  if (section->bits_per_entry == 0) {
    // All blocks are the same, no data needed
    section->data_size = 0;
    section->data = NULL;
  } else {
    int blocks_per_long = 64 / section->bits_per_entry;
    section->data_size = (4096 + blocks_per_long - 1) / blocks_per_long; // Ceiling division
    section->data = calloc(section->data_size, sizeof(uint64_t));
    
    // Pack the indices
    for (int i = 0; i < 4096; i++) {
      int long_index = i / blocks_per_long;
      int offset_in_long = (i % blocks_per_long) * section->bits_per_entry;
      section->data[long_index] |= ((uint64_t)block_indices[i]) << offset_in_long;
    }
  }
  
  return section;
}

void section_free(Section *section) {
  if (!section) return;
  
  // Free palette strings
  for (int i = 0; i < section->palette_size; i++) {
    free(section->palette[i]);
  }
  free(section->palette);
  
  // Free data array
  free(section->data);
  
  free(section);
}
