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
static Instruction *parse_instruction(Parser *parser);
static BlockPlacement parse_block_placement(Parser *parser);
static Assignment parse_assignment(Parser *parser);
static ForLoop parse_for_loop(Parser *parser);
static Occurrence parse_occurrence(Parser *parser);
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
  Expression *expr = malloc(sizeof(Expression));
  if (!expr) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  if (check(parser, TOKEN_NUMBER)) {
    expr->type = EXPR_NUMBER;
    expr->number = atof(parser->current_token.value);
    advance(parser);
    return expr;
  }

  if (check(parser, TOKEN_IDENTIFIER)) {
    expr->type = EXPR_IDENTIFIER;
    strncpy(expr->identifier, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    expr->identifier[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    advance(parser);
    return expr;
  }

  if (match(parser, TOKEN_LEFT_BRACKET)) {
    free(expr);
    return parse_coordinate(parser);
  }

  if (match(parser, TOKEN_LEFT_PAREN)) {
    free(expr);
    Expression *inner = parse_expression(parser);
    expect(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return inner;
  }

  parser_error(parser, "Expected expression");
  free(expr);
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
  
  coord->coordinate.x = parse_expression(parser);
  if (!coord->coordinate.x) {
    free(coord);
    return NULL;
  }

  // Handle optional comma
  match(parser, TOKEN_COMMA);

  coord->coordinate.z = parse_expression(parser);
  if (!coord->coordinate.z) {
    expression_free(coord->coordinate.x);
    free(coord);
    return NULL;
  }

  // Check for optional y coordinate (third value)
  coord->coordinate.y = NULL;
  if (match(parser, TOKEN_COMMA)) {
    // If we have three values, reassign: x stays x, z becomes y, new value becomes z
    coord->coordinate.y = coord->coordinate.z;
    coord->coordinate.z = parse_expression(parser);
    if (!coord->coordinate.z) {
      expression_free(coord->coordinate.x);
      expression_free(coord->coordinate.y);
      free(coord);
      return NULL;
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

// Instruction parsing
static BlockPlacement parse_block_placement(Parser *parser) {
  BlockPlacement placement = {0};
  
  placement.coordinate = parse_coordinate(parser);
  if (!placement.coordinate) return placement;

  // Parse block name
  if (!check(parser, TOKEN_IDENTIFIER)) {
    parser_error(parser, "Expected block name");
    return placement;
  }

  strncpy(placement.block_name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser);

  // Parse optional block properties [key=value,...]
  placement.block_properties[0] = '\0';
  if (match(parser, TOKEN_LEFT_BRACKET)) {
    // Read everything until ]
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

  return placement;
}

static Assignment parse_assignment(Parser *parser) {
  Assignment assignment = {0};
  
  strncpy(assignment.name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  assignment.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser); // consume identifier

  expect(parser, TOKEN_EQUAL, "Expected '=' in assignment");
  
  // Check if this is a palette or occurrence definition
  if (check(parser, TOKEN_LEFT_BRACE) || check(parser, TOKEN_HASH)) {
    // This will be handled by the caller
    assignment.value = NULL;
    return assignment;
  }

  assignment.value = parse_expression(parser);
  return assignment;
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

  if (!tokenizer_consume_value(&parser->tokenizer, TOKEN_IDENTIFIER, "in")) {
    expect(parser, TOKEN_IDENTIFIER, "Expected 'in' after loop variable");
    parser->current_token = tokenizer_peek_token(&parser->tokenizer);
  } else {
    parser->current_token = tokenizer_next_token(&parser->tokenizer);
  }

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

  strncpy(occurrence.type, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.type[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  advance(parser);

  // Parse arguments
  if (match(parser, TOKEN_LEFT_PAREN)) {
    int capacity = 4;
    occurrence.args = malloc(sizeof(Expression *) * capacity);
    occurrence.arg_count = 0;

    while (!check(parser, TOKEN_RIGHT_PAREN) && !check(parser, TOKEN_EOF)) {
      Expression *arg = parse_expression(parser);
      if (arg) {
        if (occurrence.arg_count >= capacity) {
          capacity *= 2;
          occurrence.args = realloc(occurrence.args, sizeof(Expression *) * capacity);
        }
        occurrence.args[occurrence.arg_count++] = arg;
      }
      
      if (!match(parser, TOKEN_COMMA)) break;
    }

    expect(parser, TOKEN_RIGHT_PAREN, "Expected ')' after occurrence arguments");
  }

  // Parse optional condition
  occurrence.condition = NULL;
  if (check(parser, TOKEN_RIGHT_ANGLE) || check(parser, TOKEN_LEFT_ANGLE) ||
      check(parser, TOKEN_EQUAL_EQUAL) || check(parser, TOKEN_NOT_EQUAL)) {
    occurrence.condition = parse_expression(parser);
  }

  expect(parser, TOKEN_LEFT_BRACE, "Expected '{' after occurrence header");

  // Parse body instructions
  int capacity = 8;
  occurrence.body = malloc(sizeof(Instruction *) * capacity);
  occurrence.body_count = 0;

  while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (occurrence.body_count >= capacity) {
        capacity *= 2;
        occurrence.body = realloc(occurrence.body, sizeof(Instruction *) * capacity);
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
  if (check(parser, TOKEN_HASH)) {
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
    Token next = tokenizer_peek_token(&parser->tokenizer);
    if (next.type == TOKEN_EQUAL) {
      char name[MAX_TOKEN_VALUE_LENGTH];
      strncpy(name, parser->current_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
      name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
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
      } else if (check(parser, TOKEN_HASH)) {
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
