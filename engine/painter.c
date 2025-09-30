#include "painter.h"
#include "builtin_macros.h"
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
static MacroCall parse_macro_call(Parser *parser);

// Parser initialization
void parser_init(Parser *parser, const char *input) {
  tokenizer_init(&parser->tokenizer, input);
  parser->has_error = false;
  parser->error_message[0] = '\0';
}

void parser_error(Parser *parser, const char *message) {
  if (!parser->has_error) {
    parser->has_error = true;
    snprintf(parser->error_message, sizeof(parser->error_message), "Parse error: %s", message);
  }
}

// Helper macros for cleaner code
#define peek() tokenizer_peek_token(&parser->tokenizer)
#define peek_type() peek().type
#define consume(type) tokenizer_consume(&parser->tokenizer, type)
#define consume_keyword(keyword) tokenizer_consume_value(&parser->tokenizer, TOKEN_IDENTIFIER, keyword)

// Expression parsing
static Expression *parse_primary(Parser *parser) {
  // Handle unary operators (minus and plus)
  if (peek_type() == TOKEN_MINUS || peek_type() == TOKEN_PLUS) {
    bool is_minus = peek_type() == TOKEN_MINUS;
    consume(is_minus ? TOKEN_MINUS : TOKEN_PLUS);
    
    Expression *operand = parse_primary(parser);
    if (!operand) return NULL;
    
    if (is_minus) {
      Expression *unary = malloc(sizeof(Expression));
      if (!unary) {
        parser_error(parser, "Out of memory");
        expression_free(operand);
        return NULL;
      }
      unary->type = EXPR_UNARY_OP;
      unary->unary.op = OP_NEGATE;
      unary->unary.operand = operand;
      return unary;
    }
    return operand; // Plus is a no-op
  }
  
  // Handle @function(...) syntax
  if (consume(TOKEN_AT)) {
    Token name_token = peek();
    if (name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected identifier after '@'");
      return NULL;
    }
    tokenizer_next_token(&parser->tokenizer);
    if (!consume(TOKEN_LEFT_PAREN)) {
      parser_error(parser, "Expected '(' after function name");
      return NULL;
    }
    return parse_function_call(parser, name_token.value);
  }

  // Handle numbers
  if (peek_type() == TOKEN_NUMBER) {
    Token token = tokenizer_next_token(&parser->tokenizer);
    Expression *expr = malloc(sizeof(Expression));
    if (!expr) {
      parser_error(parser, "Out of memory");
      return NULL;
    }
    expr->type = EXPR_NUMBER;
    expr->number = atof(token.value);
    return expr;
  }

  // Handle identifiers and function calls
  if (peek_type() == TOKEN_IDENTIFIER) {
    Token token = tokenizer_next_token(&parser->tokenizer);
    
    if (consume(TOKEN_LEFT_PAREN)) {
      return parse_function_call(parser, token.value);
    }

    Expression *expr = malloc(sizeof(Expression));
    if (!expr) {
      parser_error(parser, "Out of memory");
      return NULL;
    }
    expr->type = EXPR_IDENTIFIER;
    strncpy(expr->identifier, token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    expr->identifier[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    return expr;
  }

  // Handle coordinate [x y z]
  if (consume(TOKEN_LEFT_BRACKET)) {
    return parse_coordinate(parser);
  }

  // Handle parenthesized expressions
  if (consume(TOKEN_LEFT_PAREN)) {
    Expression *inner = parse_expression(parser);
    if (!inner) return NULL;
    if (!consume(TOKEN_RIGHT_PAREN)) {
      parser_error(parser, "Expected ')' after expression");
      expression_free(inner);
      return NULL;
    }
    return inner;
  }

  parser_error(parser, "Expected expression");
  return NULL;
}

static Expression *parse_multiplicative(Parser *parser) {
  Expression *left = parse_primary(parser);
  if (!left) return NULL;

  while (peek_type() == TOKEN_STAR || peek_type() == TOKEN_SLASH || peek_type() == TOKEN_MODULO) {
    BinaryOperator op;
    if (consume(TOKEN_STAR)) op = OP_MULTIPLY;
    else if (consume(TOKEN_SLASH)) op = OP_DIVIDE;
    else if (consume(TOKEN_MODULO)) op = OP_MODULO;
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

  while (peek_type() == TOKEN_PLUS || peek_type() == TOKEN_MINUS) {
    BinaryOperator op = consume(TOKEN_PLUS) ? OP_ADD : OP_SUBTRACT;
    if (op != OP_ADD) consume(TOKEN_MINUS);

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

  TokenType type = peek_type();
  if (type == TOKEN_EQUAL_EQUAL || type == TOKEN_NOT_EQUAL ||
      type == TOKEN_LEFT_ANGLE || type == TOKEN_LESS_EQUAL ||
      type == TOKEN_RIGHT_ANGLE || type == TOKEN_GREATER_EQUAL) {
    
    BinaryOperator op;
    if (consume(TOKEN_EQUAL_EQUAL)) op = OP_EQUAL;
    else if (consume(TOKEN_NOT_EQUAL)) op = OP_NOT_EQUAL;
    else if (consume(TOKEN_LEFT_ANGLE)) op = OP_LESS;
    else if (consume(TOKEN_LESS_EQUAL)) op = OP_LESS_EQUAL;
    else if (consume(TOKEN_RIGHT_ANGLE)) op = OP_GREATER;
    else if (consume(TOKEN_GREATER_EQUAL)) op = OP_GREATER_EQUAL;
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
  consume(TOKEN_COMMA);

  coord->coordinate.y = NULL;
  coord->coordinate.z = NULL;

  if (peek_type() != TOKEN_RIGHT_BRACKET) {
    // Parse second value (could be y or z depending on whether there's a third)
    Expression *second = parse_expression(parser);
    if (!second) {
      expression_free(coord->coordinate.x);
      free(coord);
      return NULL;
    }

    // Optional comma between second and third values
    consume(TOKEN_COMMA);

    if (peek_type() != TOKEN_RIGHT_BRACKET) {
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

  if (!consume(TOKEN_RIGHT_BRACKET)) {
    parser_error(parser, "Expected ']' after coordinate");
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
  if (peek_type() != TOKEN_RIGHT_PAREN && peek_type() != TOKEN_EOF) {
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

      if (!consume(TOKEN_COMMA)) {
        break;
      }
    }
  }

  if (!consume(TOKEN_RIGHT_PAREN)) {
    parser_error(parser, "Expected ')' after function arguments");
    expression_free(expr);
    return NULL;
  }

  return expr;
}
// Instruction parsing
static BlockPlacement parse_block_placement(Parser *parser) {
  BlockPlacement placement = {0};
  
  // Consume the opening [
  if (!consume(TOKEN_LEFT_BRACKET)) {
    parser_error(parser, "Expected '[' for coordinate");
    return placement;
  }
  
  placement.coordinate = parse_coordinate(parser);
  if (!placement.coordinate) return placement;

  // Parse block name (optionally with namespace, e.g., "minecraft:grass_block")
  Token name_token = peek();
  if (name_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected block name");
    return placement;
  }

  strncpy(placement.block_name, name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  tokenizer_next_token(&parser->tokenizer);

  // Check for namespace separator (:)
  if (consume(TOKEN_COLON)) {
    Token ns_token = peek();
    if (ns_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected block name after ':'");
      return placement;
    }
    // Append the colon and the rest of the name
    size_t current_len = strlen(placement.block_name);
    if (current_len < MAX_TOKEN_VALUE_LENGTH - 1) {
      placement.block_name[current_len] = ':';
      strncpy(placement.block_name + current_len + 1, 
              ns_token.value, 
              MAX_TOKEN_VALUE_LENGTH - current_len - 2);
      placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    }
    tokenizer_next_token(&parser->tokenizer);
  }

  // Parse optional block properties [key=value,...]
  placement.block_properties[0] = '\0';
  if (peek_type() == TOKEN_LEFT_BRACKET) {
    Token next = tokenizer_peek_next_token(&parser->tokenizer);
    if (next.type == TOKEN_IDENTIFIER || next.type == TOKEN_RIGHT_BRACKET) {
      tokenizer_next_token(&parser->tokenizer); // consume '['

      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = tokenizer_next_token(&parser->tokenizer);
        size_t token_len = strlen(prop_token.value);

        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          strcpy(placement.block_properties + offset, prop_token.value);
          offset += token_len;
        }
      }
      if (!consume(TOKEN_RIGHT_BRACKET)) {
        parser_error(parser, "Expected ']' after block properties");
      }
    }
  }

  return placement;
}

static ForLoop parse_for_loop(Parser *parser) {
  ForLoop loop = {0};
  
  // 'for' keyword is already consumed by caller
  
  Token var_token = peek();
  if (var_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected loop variable name");
    return loop;
  }

  strncpy(loop.variable, var_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  loop.variable[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  tokenizer_next_token(&parser->tokenizer);

  if (!consume_keyword("in")) {
    parser_error(parser, "Expected 'in' after loop variable");
    return loop;
  }

  loop.start = parse_expression(parser);
  if (!loop.start) return loop;

  if (!consume(TOKEN_DOT_DOT)) {
    parser_error(parser, "Expected '..' in range");
    expression_free(loop.start);
    return loop;
  }

  loop.end = parse_expression(parser);
  if (!loop.end) {
    expression_free(loop.start);
    return loop;
  }

  if (!consume(TOKEN_LEFT_BRACE)) {
    parser_error(parser, "Expected '{' after for loop header");
    expression_free(loop.start);
    expression_free(loop.end);
    return loop;
  }

  // Parse body instructions
  int capacity = 8;
  loop.body = malloc(sizeof(Instruction *) * capacity);
  loop.body_count = 0;

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (loop.body_count >= capacity) {
        capacity *= 2;
        loop.body = realloc(loop.body, sizeof(Instruction *) * capacity);
      }
      loop.body[loop.body_count++] = instr;
    }
  }

  if (!consume(TOKEN_RIGHT_BRACE)) {
    parser_error(parser, "Expected '}' after for loop body");
  }

  return loop;
}

static Occurrence parse_occurrence(Parser *parser) {
  Occurrence occurrence = {0};

  tokenizer_next_token(&parser->tokenizer); // consume '@'

  Token type_token = peek();
  if (type_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  char type_name[MAX_TOKEN_VALUE_LENGTH];
  strncpy(type_name, type_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  type_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  tokenizer_next_token(&parser->tokenizer);

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

  if (consume(TOKEN_LEFT_PAREN)) {
    int capacity = 4;
    occurrence.args = malloc(sizeof(Expression *) * capacity);
    if (!occurrence.args) {
      parser_error(parser, "Out of memory");
      return occurrence;
    }

    while (peek_type() != TOKEN_RIGHT_PAREN && peek_type() != TOKEN_EOF) {
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

      if (!consume(TOKEN_COMMA)) {
        break;
      }
    }

    if (!consume(TOKEN_RIGHT_PAREN)) {
      parser_error(parser, "Expected ')' after occurrence arguments");
      return occurrence;
    }
  }

  occurrence.condition = NULL;
  TokenType type = peek_type();
  if (type == TOKEN_RIGHT_ANGLE || type == TOKEN_LEFT_ANGLE ||
      type == TOKEN_EQUAL_EQUAL || type == TOKEN_NOT_EQUAL ||
      type == TOKEN_GREATER_EQUAL || type == TOKEN_LESS_EQUAL) {
    occurrence.condition = parse_expression(parser);
  }

  if (!consume(TOKEN_LEFT_BRACE)) {
    parser_error(parser, "Expected '{' after occurrence header");
    return occurrence;
  }

  int body_capacity = 8;
  occurrence.body = malloc(sizeof(Instruction *) * body_capacity);
  occurrence.body_count = 0;

  if (!occurrence.body) {
    parser_error(parser, "Out of memory");
    return occurrence;
  }

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
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

  if (!consume(TOKEN_RIGHT_BRACE)) {
    parser_error(parser, "Expected '}' after occurrence body");
  }

  return occurrence;
}

static PaletteDefinition parse_palette_definition(Parser *parser) {
  PaletteDefinition palette = {0};
  
  // Name is already parsed in assignment
  if (!consume(TOKEN_LEFT_BRACE)) {
    parser_error(parser, "Expected '{' for palette definition");
    return palette;
  }

  int capacity = 16;
  palette.entries = malloc(sizeof(PaletteEntry) * capacity);
  palette.entry_count = 0;

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
    // Parse key
    Token key_token = peek();
    if (key_token.type != TOKEN_NUMBER) {
      parser_error(parser, "Expected number for palette key");
      break;
    }

    PaletteEntry entry = {0};
    entry.key = atoi(key_token.value);
    tokenizer_next_token(&parser->tokenizer);

    if (!consume(TOKEN_COLON)) {
      parser_error(parser, "Expected ':' after palette key");
      break;
    }

    // Parse block name
    Token block_token = peek();
    if (block_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected block name");
      break;
    }

    strncpy(entry.block_name, block_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    entry.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    tokenizer_next_token(&parser->tokenizer);

    // Parse optional properties
    entry.block_properties[0] = '\0';
    if (consume(TOKEN_LEFT_BRACKET)) {
      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = tokenizer_next_token(&parser->tokenizer);
        size_t token_len = strlen(prop_token.value);
        
        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          strcpy(entry.block_properties + offset, prop_token.value);
          offset += token_len;
        }
      }
      if (!consume(TOKEN_RIGHT_BRACKET)) {
        parser_error(parser, "Expected ']' after block properties");
      }
    }

    if (palette.entry_count >= capacity) {
      capacity *= 2;
      palette.entries = realloc(palette.entries, sizeof(PaletteEntry) * capacity);
    }
    palette.entries[palette.entry_count++] = entry;

    // Optional comma or newline
    consume(TOKEN_COMMA);
  }

  if (!consume(TOKEN_RIGHT_BRACE)) {
    parser_error(parser, "Expected '}' after palette definition");
  }

  return palette;
}
static MacroCall parse_macro_call(Parser *parser) {
  MacroCall macro = {0};
  
  // Consume '#'
  tokenizer_next_token(&parser->tokenizer);
  
  // Parse macro name
  Token name_token = peek();
  if (name_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected macro name after '#'");
    return macro;
  }
  
  strncpy(macro.name, name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  macro.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  tokenizer_next_token(&parser->tokenizer);
  
  // Parse arguments (.name=value)
  int capacity = 8;
  macro.arguments = malloc(sizeof(MacroArgument) * capacity);
  macro.argument_count = 0;
  
  if (!macro.arguments) {
    parser_error(parser, "Out of memory");
    return macro;
  }
  
  while (consume(TOKEN_DOT)) {
    Token arg_name_token = peek();
    if (arg_name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected argument name after '.'");
      return macro;
    }
    
    MacroArgument arg = {0};
    strncpy(arg.name, arg_name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    arg.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    tokenizer_next_token(&parser->tokenizer);
    
    if (!consume(TOKEN_EQUAL)) {
      parser_error(parser, "Expected '=' after argument name");
      return macro;
    }
    
    arg.value = parse_expression(parser);
    if (!arg.value) {
      return macro;
    }
    
    if (macro.argument_count >= capacity) {
      capacity *= 2;
      MacroArgument *resized = realloc(macro.arguments, sizeof(MacroArgument) * capacity);
      if (!resized) {
        parser_error(parser, "Out of memory");
        return macro;
      }
      macro.arguments = resized;
    }
    
    macro.arguments[macro.argument_count++] = arg;
  }
  
  return macro;
}

static Instruction *parse_instruction(Parser *parser) {
  if (peek_type() == TOKEN_EOF) return NULL;

  Instruction *instr = malloc(sizeof(Instruction));
  if (!instr) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  // Block placement: [x y z] block_name
  if (peek_type() == TOKEN_LEFT_BRACKET) {
    instr->type = INSTR_BLOCK_PLACEMENT;
    instr->block_placement = parse_block_placement(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // For loop: for var in start..end { ... }
  if (consume_keyword("for")) {
    instr->type = INSTR_FOR_LOOP;
    instr->for_loop = parse_for_loop(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // Occurrence: @type(...) { ... } or variable = @type(...)
  if (peek_type() == TOKEN_AT) {
    instr->type = INSTR_OCCURRENCE;
    instr->occurrence = parse_occurrence(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // Macro call: #macro_name .arg1=value1 .arg2=value2
  if (peek_type() == TOKEN_HASH) {
    instr->type = INSTR_MACRO_CALL;
    instr->macro_call = parse_macro_call(parser);
    if (parser->has_error) {
      free(instr);
      return NULL;
    }
    return instr;
  }

  // Assignment or palette definition: name = value or name = { ... }
  if (peek_type() == TOKEN_IDENTIFIER) {
    Token name_token = tokenizer_next_token(&parser->tokenizer);
    char name[MAX_TOKEN_VALUE_LENGTH];
    strncpy(name, name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';

    if (consume(TOKEN_EQUAL)) {
      // Check if this is a palette definition or occurrence assignment
      if (peek_type() == TOKEN_LEFT_BRACE) {
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
      } else if (peek_type() == TOKEN_AT) {
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
    } else if (peek_type() == TOKEN_LEFT_BRACE) {
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

  while (peek_type() != TOKEN_EOF && !parser->has_error) {
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
    case EXPR_UNARY_OP:
      expression_free(expr->unary.operand);
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
    case INSTR_MACRO_CALL:
      for (int i = 0; i < instr->macro_call.argument_count; i++) {
        expression_free(instr->macro_call.arguments[i].value);
      }
      free(instr->macro_call.arguments);
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

// Macro registry implementation
void macro_registry_init(MacroRegistry *registry) {
  registry->entry_capacity = 16;
  registry->entry_count = 0;
  registry->entries = malloc(sizeof(MacroRegistryEntry) * registry->entry_capacity);
}

void macro_registry_register(MacroRegistry *registry, const char *name, MacroGenerator generator) {
  if (registry->entry_count >= registry->entry_capacity) {
    registry->entry_capacity *= 2;
    registry->entries = realloc(registry->entries, sizeof(MacroRegistryEntry) * registry->entry_capacity);
  }
  
  strncpy(registry->entries[registry->entry_count].name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  registry->entries[registry->entry_count].name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  registry->entries[registry->entry_count].generator = generator;
  registry->entry_count++;
}

MacroGenerator macro_registry_lookup(MacroRegistry *registry, const char *name) {
  for (int i = 0; i < registry->entry_count; i++) {
    if (strcmp(registry->entries[i].name, name) == 0) {
      return registry->entries[i].generator;
    }
  }
  return NULL;
}

void macro_registry_free(MacroRegistry *registry) {
  free(registry->entries);
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

// Variable context implementation (moving from static to exported)
void context_init(VariableContext *ctx) {
  ctx->variable_capacity = 16;
  ctx->variable_count = 0;
  ctx->variables = malloc(sizeof(Variable) * ctx->variable_capacity);
}

void context_free(VariableContext *ctx) {
  free(ctx->variables);
}

void context_set(VariableContext *ctx, const char *name, double value) {
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

double context_get(VariableContext *ctx, const char *name) {
  for (int i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      return ctx->variables[i].value;
    }
  }
  return 0.0;
}

// Helper function to get macro argument by name
Expression *macro_get_arg(MacroArgument *args, int arg_count, const char *name) {
  for (int i = 0; i < arg_count; i++) {
    if (strcmp(args[i].name, name) == 0) {
      return args[i].value;
    }
  }
  return NULL;
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
    case EXPR_UNARY_OP: {
      double operand = evaluate_expression(expr->unary.operand, ctx);
      switch (expr->unary.op) {
        case OP_NEGATE: return -operand;
        default: return 0.0;
      }
    }
    default:
      return 0.0;
  }
}

// Forward declaration for recursive processing
static void process_instruction(Instruction *instr, VariableContext *ctx, 
                                MacroRegistry *macro_registry,
                                int base_x, int base_y, int base_z,
                                int *block_indices, char ***palette, 
                                int *palette_size, int *palette_capacity);


// Process a single instruction and its effects on the section
static void process_instruction(Instruction *instr, VariableContext *ctx,
                                MacroRegistry *macro_registry,
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
          process_instruction(loop->body[j], ctx, macro_registry, base_x, base_y, base_z,
                            block_indices, palette, palette_size, palette_capacity);
        }
      }
      break;
    }
    
    case INSTR_MACRO_CALL: {
      MacroCall *macro_call = &instr->macro_call;
      
      // Look up the macro generator
      MacroGenerator generator = macro_registry_lookup(macro_registry, macro_call->name);
      if (generator) {
        // Execute the macro
        generator(ctx, macro_call->arguments, macro_call->argument_count,
                 base_x, base_y, base_z, block_indices, palette, 
                 palette_size, palette_capacity);
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
  
  // Initialize and register built-in macros
  MacroRegistry macro_registry;
  macro_registry_init(&macro_registry);
  register_builtin_macros(&macro_registry);
  
  // Process all instructions
  for (int i = 0; i < program->instruction_count; i++) {
    process_instruction(program->instructions[i], &ctx, &macro_registry,
                       base_x, base_y, base_z,
                       block_indices, &section->palette, 
                       &section->palette_size, &palette_capacity);
  }
  
  // Clean up
  context_free(&ctx);
  macro_registry_free(&macro_registry);
  
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
