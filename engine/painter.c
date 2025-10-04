#include "painter.h"
#include "painter_eval.h"
#include "builtin_functions.h"
#include "builtin_macros.h"
#include "builtin_occurrences.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration for bounds tracking
static void update_instruction_bounds(InstructionList *list, const Instruction *instr, int conservative_range);

// -----------------------------------------------------------------------------
// Internal utility helpers
// -----------------------------------------------------------------------------

static bool ensure_capacity(Parser *parser, void **buffer, size_t *capacity, size_t count, size_t element_size) {
  if (*capacity > count) {
    return true;
  }

  size_t new_capacity = (*capacity == 0) ? 4 : (*capacity * 2);
  void *resized = realloc(*buffer, new_capacity * element_size);
  if (!resized) {
    if (parser) {
      parser_error(parser, "Out of memory");
    }
    return false;
  }

  *buffer = resized;
  *capacity = new_capacity;
  return true;
}

static inline void instruction_list_reset(InstructionList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
  list->has_bounds = false;
  list->min_x = list->max_x = 0;
  list->min_y = list->max_y = 0;
  list->min_z = list->max_z = 0;
}

static inline void expression_list_reset(ExpressionList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

static inline void named_argument_list_reset(NamedArgumentList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

static inline void palette_entry_list_reset(PaletteEntryList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

static bool instruction_list_push(Parser *parser, InstructionList *list, Instruction *instr) {
  if (!ensure_capacity(parser, (void **)&list->items, &list->capacity, list->count, sizeof(*list->items))) {
    return false;
  }
  list->items[list->count++] = instr;
  // Update bounds with conservative range of 100 blocks (adjustable)
  update_instruction_bounds(list, instr, 100);
  return true;
}

static bool expression_list_push(Parser *parser, ExpressionList *list, Expression *expr) {
  if (!ensure_capacity(parser, (void **)&list->items, &list->capacity, list->count, sizeof(*list->items))) {
    return false;
  }
  list->items[list->count++] = expr;
  return true;
}

static bool named_argument_list_push(Parser *parser, NamedArgumentList *list, NamedArgument argument) {
  if (!ensure_capacity(parser, (void **)&list->items, &list->capacity, list->count, sizeof(*list->items))) {
    return false;
  }
  list->items[list->count++] = argument;
  return true;
}

static bool palette_entry_list_push(Parser *parser, PaletteEntryList *list, PaletteEntry entry) {
  if (!ensure_capacity(parser, (void **)&list->items, &list->capacity, list->count, sizeof(*list->items))) {
    return false;
  }
  list->items[list->count++] = entry;
  return true;
}

static int palette_entry_compare(const void *a, const void *b) {
  const PaletteEntry *entry_a = (const PaletteEntry *)a;
  const PaletteEntry *entry_b = (const PaletteEntry *)b;
  return entry_a->key - entry_b->key;
}

static bool program_ensure_capacity(Parser *parser, Program *program) {
  if (program->instruction_count < program->instruction_capacity) {
    return true;
  }

  int new_capacity = (program->instruction_capacity == 0) ? 32 : program->instruction_capacity * 2;
  Instruction **resized = realloc(program->instructions, sizeof(*program->instructions) * new_capacity);
  if (!resized) {
    if (parser) {
      parser_error(parser, "Out of memory");
    }
    return false;
  }

  program->instructions = resized;
  program->instruction_capacity = new_capacity;
  return true;
}

static bool program_push_instruction(Parser *parser, Program *program, Instruction *instr) {
  if (!program_ensure_capacity(parser, program)) {
    return false;
  }
  program->instructions[program->instruction_count++] = instr;
  return true;
}

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
static IfStatement parse_if_statement(Parser *parser);
static bool parse_occurrence_header(Parser *parser, Occurrence *occurrence);
static bool parse_occurrence_body(Parser *parser, Occurrence *occurrence);
static Occurrence parse_occurrence(Parser *parser);
static Occurrence parse_occurrence_definition(Parser *parser, const char *name);
static Occurrence parse_occurrence_reference(Parser *parser, const char *name);
static PaletteDefinition parse_palette_definition(Parser *parser);
static MacroCall parse_macro_call(Parser *parser);
static void update_instruction_bounds(InstructionList *list, const Instruction *instr, int conservative_range);

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
#define next() tokenizer_next_token(&parser->tokenizer)
#define consume(type) tokenizer_consume(&parser->tokenizer, type)
#define consume_keyword(keyword) tokenizer_consume_value(&parser->tokenizer, TOKEN_IDENTIFIER, keyword)

// Helper function to expect a token with error handling
static inline bool expect_token(Parser *parser, TokenType type, const char *error_msg) {
  if (consume(type)) return true;
  parser_error(parser, error_msg);
  return false;
}

// Helper function to allocate with error handling
static inline void *alloc_or_error(Parser *parser, size_t size, const char *error_msg) {
  void *ptr = malloc(size);
  if (!ptr) parser_error(parser, error_msg);
  return ptr;
}

// Expression builder helpers
static inline Expression *make_number(double value) {
  Expression *expr = malloc(sizeof(Expression));
  if (expr) {
    expr->type = EXPR_NUMBER;
    expr->number = value;
  }
  return expr;
}

static inline Expression *make_identifier(const char *name) {
  Expression *expr = malloc(sizeof(Expression));
  if (expr) {
    expr->type = EXPR_IDENTIFIER;
    strncpy(expr->identifier, name, MAX_TOKEN_VALUE_LENGTH - 1);
    expr->identifier[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  }
  return expr;
}

static inline Expression *make_binary_op(BinaryOperator op, Expression *left, Expression *right) {
  Expression *expr = malloc(sizeof(Expression));
  if (expr) {
    expr->type = EXPR_BINARY_OP;
    expr->binary.op = op;
    expr->binary.left = left;
    expr->binary.right = right;
  }
  return expr;
}

static inline Expression *make_unary_op(UnaryOperator op, Expression *operand) {
  Expression *expr = malloc(sizeof(Expression));
  if (expr) {
    expr->type = EXPR_UNARY_OP;
    expr->unary.op = op;
    expr->unary.operand = operand;
  }
  return expr;
}

// Expression parsing
static Expression *parse_primary(Parser *parser) {
  // Handle unary operators (minus and plus)
  if (peek_type() == TOKEN_MINUS || peek_type() == TOKEN_PLUS) {
    bool is_minus = peek_type() == TOKEN_MINUS;
    consume(is_minus ? TOKEN_MINUS : TOKEN_PLUS);

    Expression *operand = parse_primary(parser);
    if (!operand) return NULL;

    // Plus is a no-op, just return the operand
    if (!is_minus) return operand;

    Expression *unary = make_unary_op(OP_NEGATE, operand);
    if (!unary) {
      parser_error(parser, "Out of memory");
      expression_free(operand);
    }
    return unary;
  }

  // Handle @function(...) syntax
  if (consume(TOKEN_AT)) {
    Token name_token = peek();
    if (name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected identifier after '@'");
      return NULL;
    }
    next();
    if (!expect_token(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name")) {
      return NULL;
    }
    return parse_function_call(parser, name_token.value);
  }

  // Handle numbers
  if (peek_type() == TOKEN_NUMBER) {
    Token token = next();
    Expression *expr = make_number(atof(token.value));
    if (!expr) parser_error(parser, "Out of memory");
    return expr;
  }

  // Handle identifiers and function calls
  if (peek_type() == TOKEN_IDENTIFIER) {
    Token token = next();

    if (consume(TOKEN_LEFT_PAREN)) {
      return parse_function_call(parser, token.value);
    }

    Expression *expr = make_identifier(token.value);
    if (!expr) parser_error(parser, "Out of memory");
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
    if (!expect_token(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression")) {
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
    if (consume(TOKEN_STAR))
      op = OP_MULTIPLY;
    else if (consume(TOKEN_SLASH))
      op = OP_DIVIDE;
    else if (consume(TOKEN_MODULO))
      op = OP_MODULO;
    else
      break;

    Expression *right = parse_primary(parser);
    if (!right) {
      expression_free(left);
      return NULL;
    }

    Expression *binary = make_binary_op(op, left, right);
    if (!binary) {
      parser_error(parser, "Out of memory");
      expression_free(left);
      expression_free(right);
      return NULL;
    }
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

    Expression *binary = make_binary_op(op, left, right);
    if (!binary) {
      parser_error(parser, "Out of memory");
      expression_free(left);
      expression_free(right);
      return NULL;
    }
    left = binary;
  }

  return left;
}

static Expression *parse_comparison(Parser *parser) {
  Expression *left = parse_additive(parser);
  if (!left) return NULL;

  TokenType type = peek_type();
  if (type == TOKEN_EQUAL_EQUAL || type == TOKEN_NOT_EQUAL || type == TOKEN_LEFT_ANGLE || type == TOKEN_LESS_EQUAL ||
      type == TOKEN_RIGHT_ANGLE || type == TOKEN_GREATER_EQUAL) {

    BinaryOperator op;
    if (consume(TOKEN_EQUAL_EQUAL))
      op = OP_EQUAL;
    else if (consume(TOKEN_NOT_EQUAL))
      op = OP_NOT_EQUAL;
    else if (consume(TOKEN_LEFT_ANGLE))
      op = OP_LESS;
    else if (consume(TOKEN_LESS_EQUAL))
      op = OP_LESS_EQUAL;
    else if (consume(TOKEN_RIGHT_ANGLE))
      op = OP_GREATER;
    else if (consume(TOKEN_GREATER_EQUAL))
      op = OP_GREATER_EQUAL;
    else
      return left;

    Expression *right = parse_additive(parser);
    if (!right) {
      expression_free(left);
      return NULL;
    }

    Expression *binary = make_binary_op(op, left, right);
    if (!binary) {
      parser_error(parser, "Out of memory");
      expression_free(left);
      expression_free(right);
      return NULL;
    }
    return binary;
  }

  return left;
}

static Expression *parse_expression(Parser *parser) { return parse_comparison(parser); }

static Expression *parse_coordinate(Parser *parser) {
  // Expects [ is already consumed
  Expression *coord = alloc_or_error(parser, sizeof(Expression), "Out of memory");
  if (!coord) return NULL;

  coord->type = EXPR_COORDINATE;
  coord->coordinate.y = NULL;
  coord->coordinate.z = NULL;

  // Parse first value (always x)
  coord->coordinate.x = parse_expression(parser);
  if (!coord->coordinate.x) goto error;

  // Handle optional comma after first expression (allow both "[x z]" and "[x, z]")
  consume(TOKEN_COMMA);

  if (peek_type() != TOKEN_RIGHT_BRACKET) {
    // Parse second value (could be y or z depending on whether there's a third)
    Expression *second = parse_expression(parser);
    if (!second) goto error;

    // Optional comma between second and third values
    consume(TOKEN_COMMA);

    if (peek_type() != TOKEN_RIGHT_BRACKET) {
      // Three values provided: [x y z]
      coord->coordinate.y = second;
      coord->coordinate.z = parse_expression(parser);
      if (!coord->coordinate.z) goto error;
    } else {
      // Two values provided: [x z], y defaults to 0
      coord->coordinate.z = second;
    }
  }

  if (!expect_token(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after coordinate")) {
    goto error;
  }

  return coord;

error:
  expression_free(coord->coordinate.x);
  expression_free(coord->coordinate.y);
  expression_free(coord->coordinate.z);
  free(coord);
  return NULL;
}

static Expression *parse_function_call(Parser *parser, const char *name) {
  Expression *expr = alloc_or_error(parser, sizeof(Expression), "Out of memory");
  if (!expr) return NULL;

  expr->type = EXPR_FUNCTION_CALL;
  strncpy(expr->function_call.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  expr->function_call.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  expression_list_reset(&expr->function_call.args);

  // Parse arguments if any
  if (peek_type() != TOKEN_RIGHT_PAREN && peek_type() != TOKEN_EOF) {
    while (true) {
      Expression *arg = parse_expression(parser);
      if (!arg) {
        expression_free(expr);
        return NULL;
      }

      if (!expression_list_push(parser, &expr->function_call.args, arg)) {
        expression_free(arg);
        expression_free(expr);
        return NULL;
      }

      if (!consume(TOKEN_COMMA)) break;
    }
  }

  if (!expect_token(parser, TOKEN_RIGHT_PAREN, "Expected ')' after function arguments")) {
    expression_free(expr);
    return NULL;
  }

  return expr;
} // Instruction parsing
static BlockPlacement parse_block_placement(Parser *parser) {
  BlockPlacement placement = {0};

  // Consume the opening [
  if (!expect_token(parser, TOKEN_LEFT_BRACKET, "Expected '[' for coordinate")) {
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
  next();

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
      strncpy(placement.block_name + current_len + 1, ns_token.value, MAX_TOKEN_VALUE_LENGTH - current_len - 2);
      placement.block_name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    }
    next();
  }

  // Parse optional block properties [key=value,...]
  placement.block_properties[0] = '\0';
  if (peek_type() == TOKEN_LEFT_BRACKET) {
    Token next_token = tokenizer_peek_next_token(&parser->tokenizer);
    if (next_token.type == TOKEN_IDENTIFIER || next_token.type == TOKEN_RIGHT_BRACKET) {
      next(); // consume '['

      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = next();
        size_t token_len = strlen(prop_token.value);

        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          strcpy(placement.block_properties + offset, prop_token.value);
          offset += token_len;
        }
      }
      if (!expect_token(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after block properties")) {
        parser_error(parser, "Expected ']' after block properties");
      }
    }
  }

  return placement;
}

static ForLoop parse_for_loop(Parser *parser) {
  ForLoop loop = {0};
  instruction_list_reset(&loop.body);

  // 'for' keyword is already consumed by caller

  Token var_token = peek();
  if (var_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected loop variable name");
    return loop;
  }

  strncpy(loop.variable, var_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  loop.variable[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  next();

  if (!consume_keyword("in")) {
    parser_error(parser, "Expected 'in' after loop variable");
    return loop;
  }

  loop.start = parse_expression(parser);
  if (!loop.start) return loop;

  if (!expect_token(parser, TOKEN_DOT_DOT, "Expected '..' in range")) {
    expression_free(loop.start);
    return loop;
  }

  loop.end = parse_expression(parser);
  if (!loop.end) {
    expression_free(loop.start);
    return loop;
  }

  if (!expect_token(parser, TOKEN_LEFT_BRACE, "Expected '{' after for loop header")) {
    expression_free(loop.start);
    expression_free(loop.end);
    return loop;
  }

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
    Instruction *instr = parse_instruction(parser);
    if (!instr) {
      if (parser->has_error) break;
      continue;
    }
    if (!instruction_list_push(parser, &loop.body, instr)) {
      instruction_free(instr);
      break;
    }
  }

  expect_token(parser, TOKEN_RIGHT_BRACE, "Expected '}' after for loop body");

  return loop;
}

static IfStatement parse_if_statement(Parser *parser) {
  IfStatement if_stmt = {0};
  if_stmt.branches = NULL;
  if_stmt.branch_count = 0;
  if_stmt.branch_capacity = 0;

  // 'if' keyword is already consumed by caller

  while (true) {
    // Allocate space for new branch
    if (if_stmt.branch_count >= if_stmt.branch_capacity) {
      size_t new_capacity = if_stmt.branch_capacity == 0 ? 2 : if_stmt.branch_capacity * 2;
      ConditionalBranch *new_branches = realloc(if_stmt.branches, new_capacity * sizeof(ConditionalBranch));
      if (!new_branches) {
        parser_error(parser, "Out of memory");
        return if_stmt;
      }
      if_stmt.branches = new_branches;
      if_stmt.branch_capacity = new_capacity;
    }

    ConditionalBranch *branch = &if_stmt.branches[if_stmt.branch_count];
    instruction_list_reset(&branch->body);

    // Check if this is an 'else' branch (no condition)
    if (if_stmt.branch_count > 0 && consume_keyword("else")) {
      branch->condition = NULL;

      // Check if this is 'else' without 'if' (final else)
      if (!consume_keyword("if")) {
        // Final else branch - must have body
        if (!expect_token(parser, TOKEN_LEFT_BRACE, "Expected '{' after else")) {
          return if_stmt;
        }

        // Parse else body
        while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
          Instruction *instr = parse_instruction(parser);
          if (!instr) {
            if (parser->has_error) break;
            continue;
          }
          if (!instruction_list_push(parser, &branch->body, instr)) {
            instruction_free(instr);
            break;
          }
        }

        expect_token(parser, TOKEN_RIGHT_BRACE, "Expected '}' after else body");
        if_stmt.branch_count++;
        break; // else is always the last branch
      }
      // else if - fall through to parse condition
    }

    // Parse condition for if/elif
    if (!expect_token(parser, TOKEN_LEFT_PAREN, "Expected '(' after if/elif")) {
      return if_stmt;
    }

    branch->condition = parse_expression(parser);
    if (!branch->condition) {
      return if_stmt;
    }

    if (!expect_token(parser, TOKEN_RIGHT_PAREN, "Expected ')' after if condition")) {
      expression_free(branch->condition);
      return if_stmt;
    }

    // Parse body
    if (!expect_token(parser, TOKEN_LEFT_BRACE, "Expected '{' after if condition")) {
      expression_free(branch->condition);
      return if_stmt;
    }

    while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
      Instruction *instr = parse_instruction(parser);
      if (!instr) {
        if (parser->has_error) break;
        continue;
      }
      if (!instruction_list_push(parser, &branch->body, instr)) {
        instruction_free(instr);
        break;
      }
    }

    expect_token(parser, TOKEN_RIGHT_BRACE, "Expected '}' after if body");
    if_stmt.branch_count++;

    // Check for elif or else
    Token next_tok = peek();
    if (next_tok.type != TOKEN_IDENTIFIER) {
      break; // No more branches
    }

    if (strcmp(next_tok.value, "elif") == 0) {
      next(); // Consume 'elif'
      // Continue loop to parse elif as a new branch with condition
      continue;
    } else if (strcmp(next_tok.value, "else") == 0) {
      // Don't consume 'else' yet - let next iteration handle it
      continue;
    } else {
      // Not elif or else, we're done
      break;
    }
  }

  return if_stmt;
}

static bool parse_occurrence_header(Parser *parser, Occurrence *occurrence) {
  named_argument_list_reset(&occurrence->args);
  occurrence->condition = NULL;

  // Parse arguments (.name=value)
  while (consume(TOKEN_DOT)) {
    Token arg_name_token = peek();
    if (arg_name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected argument name after '.'");
      return false;
    }

    NamedArgument arg = {0};
    strncpy(arg.name, arg_name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    arg.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    next();

    if (!expect_token(parser, TOKEN_EQUAL, "Expected '=' after argument name")) {
      return false;
    }

    arg.value = parse_expression(parser);
    if (!arg.value) {
      return false;
    }

    if (!named_argument_list_push(parser, &occurrence->args, arg)) {
      expression_free(arg.value);
      return false;
    }
  }

  TokenType type = peek_type();
  if (type == TOKEN_RIGHT_ANGLE || type == TOKEN_LEFT_ANGLE || type == TOKEN_EQUAL_EQUAL || type == TOKEN_NOT_EQUAL ||
      type == TOKEN_GREATER_EQUAL || type == TOKEN_LESS_EQUAL) {
    occurrence->condition = parse_expression(parser);
    if (!occurrence->condition) {
      return false;
    }
  }

  return true;
}

static bool parse_occurrence_body(Parser *parser, Occurrence *occurrence) {
  instruction_list_reset(&occurrence->body);

  if (!expect_token(parser, TOKEN_LEFT_BRACE, "Expected '{' after occurrence header")) {
    return false;
  }

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
    Instruction *instr = parse_instruction(parser);
    if (!instr) {
      if (parser->has_error) break;
      continue;
    }
    if (!instruction_list_push(parser, &occurrence->body, instr)) {
      instruction_free(instr);
      return false;
    }
  }

  if (!consume(TOKEN_RIGHT_BRACE)) {
    parser_error(parser, "Expected '}' after occurrence body");
    return false;
  }

  return true;
}

static Occurrence parse_occurrence(Parser *parser) {
  Occurrence occurrence = {0};
  occurrence.kind = OCCURRENCE_KIND_IMMEDIATE;
  occurrence.name[0] = '\0';
  occurrence.condition = NULL;
  named_argument_list_reset(&occurrence.args);
  instruction_list_reset(&occurrence.body);

  next(); // consume '@'

  Token type_token = peek();
  if (type_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  strncpy(occurrence.type, type_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.type[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  next();

  if (!parse_occurrence_header(parser, &occurrence)) {
    return occurrence;
  }

  if (!parse_occurrence_body(parser, &occurrence)) {
    return occurrence;
  }

  return occurrence;
}

static Occurrence parse_occurrence_definition(Parser *parser, const char *name) {
  Occurrence occurrence = {0};
  occurrence.kind = OCCURRENCE_KIND_DEFINITION;
  strncpy(occurrence.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  occurrence.condition = NULL;
  named_argument_list_reset(&occurrence.args);
  instruction_list_reset(&occurrence.body);

  if (!consume(TOKEN_AT)) {
    parser_error(parser, "Expected '@' for occurrence definition");
    return occurrence;
  }

  Token type_token = peek();
  if (type_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  strncpy(occurrence.type, type_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.type[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  next();

  if (!parse_occurrence_header(parser, &occurrence)) {
    return occurrence;
  }

  return occurrence;
}

static Occurrence parse_occurrence_reference(Parser *parser, const char *name) {
  Occurrence occurrence = {0};
  occurrence.kind = OCCURRENCE_KIND_REFERENCE;
  strncpy(occurrence.name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  occurrence.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  occurrence.type[0] = '\0';
  occurrence.condition = NULL;
  named_argument_list_reset(&occurrence.args);
  instruction_list_reset(&occurrence.body);

  if (!parse_occurrence_body(parser, &occurrence)) {
    return occurrence;
  }

  return occurrence;
}

static PaletteDefinition parse_palette_definition(Parser *parser) {
  PaletteDefinition palette = {0};
  palette_entry_list_reset(&palette.entries);

  // Name is already parsed in assignment
  if (!expect_token(parser, TOKEN_LEFT_BRACE, "Expected '{' for palette definition")) {
    return palette;
  }

  while (peek_type() != TOKEN_RIGHT_BRACE && peek_type() != TOKEN_EOF) {
    // Parse key
    Token key_token = peek();
    if (key_token.type != TOKEN_NUMBER) {
      parser_error(parser, "Expected number for palette key");
      break;
    }

    PaletteEntry entry = {0};
    entry.key = atoi(key_token.value);
    if (entry.key < 0) {
      parser_error(parser, "Palette index must be non-negative");
      break;
    }
    next();

    if (!expect_token(parser, TOKEN_COLON, "Expected ':' after palette key")) {
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
    next();

    // Parse optional properties
    entry.block_properties[0] = '\0';
    if (consume(TOKEN_LEFT_BRACKET)) {
      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = next();
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

    if (!palette_entry_list_push(parser, &palette.entries, entry)) {
      break;
    }

    // Optional comma or newline
    consume(TOKEN_COMMA);
  }

  if (!parser->has_error && palette.entries.count > 0) {
    if (palette.entries.count > 1) {
      qsort(palette.entries.items, palette.entries.count, sizeof(PaletteEntry), palette_entry_compare);
    }

    for (size_t i = 0; i < palette.entries.count; i++) {
      PaletteEntry *entry = &palette.entries.items[i];
      if (entry->key != (int)i) {
        parser_error(parser, "Palette indices must be contiguous starting at 0");
        break;
      }
      entry->key = (int)i;
    }
  }

  if (!expect_token(parser, TOKEN_RIGHT_BRACE, "Expected '}' after palette definition")) {
    // error already set
  }

  return palette;
}

static MacroCall parse_macro_call(Parser *parser) {
  MacroCall macro = {0};
  named_argument_list_reset(&macro.arguments);

  // Consume '#'
  next();

  // Parse macro name
  Token name_token = peek();
  if (name_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected macro name after '#'");
    return macro;
  }

  strncpy(macro.name, name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
  macro.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  next();

  // Parse arguments (.name=value)
  while (consume(TOKEN_DOT)) {
    Token arg_name_token = peek();
    if (arg_name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected argument name after '.'");
      return macro;
    }

    NamedArgument arg = {0};
    strncpy(arg.name, arg_name_token.value, MAX_TOKEN_VALUE_LENGTH - 1);
    arg.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
    next();

    if (!expect_token(parser, TOKEN_EQUAL, "Expected '=' after argument name")) {
      return macro;
    }

    arg.value = parse_expression(parser);
    if (!arg.value) {
      return macro;
    }

    if (!named_argument_list_push(parser, &macro.arguments, arg)) {
      expression_free(arg.value);
      return macro;
    }
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

  // If statement: if(...) { ... } elif(...) { ... } else { ... }
  if (consume_keyword("if")) {
    instr->type = INSTR_IF_STATEMENT;
    instr->if_statement = parse_if_statement(parser);
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
    Token name_token = next();
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
        instr->occurrence = parse_occurrence_definition(parser, name);
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
      instr->occurrence = parse_occurrence_reference(parser, name);
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

  program->instructions = NULL;
  program->instruction_count = 0;
  program->instruction_capacity = 0;

  while (peek_type() != TOKEN_EOF && !parser->has_error) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (!program_push_instruction(parser, program, instr)) {
        instruction_free(instr);
        break;
      }
    } else if (parser->has_error) {
      break;
    }
  }

  if (parser->has_error) {
    fprintf(stderr, "%s\n", parser->error_message);
  }

  return program;
}

// Deep copy an expression
static Expression *expression_copy(const Expression *expr) {
  if (!expr) return NULL;

  Expression *copy = malloc(sizeof(Expression));
  if (!copy) return NULL;

  copy->type = expr->type;

  switch (expr->type) {
  case EXPR_NUMBER: copy->number = expr->number; break;
  case EXPR_IDENTIFIER: strncpy(copy->identifier, expr->identifier, MAX_TOKEN_VALUE_LENGTH); break;
  case EXPR_BINARY_OP:
    copy->binary.op = expr->binary.op;
    copy->binary.left = expression_copy(expr->binary.left);
    copy->binary.right = expression_copy(expr->binary.right);
    if (!copy->binary.left || !copy->binary.right) {
      expression_free(copy);
      return NULL;
    }
    break;
  case EXPR_UNARY_OP:
    copy->unary.op = expr->unary.op;
    copy->unary.operand = expression_copy(expr->unary.operand);
    if (!copy->unary.operand) {
      expression_free(copy);
      return NULL;
    }
    break;
  case EXPR_COORDINATE:
    copy->coordinate.x = expression_copy(expr->coordinate.x);
    copy->coordinate.y = expression_copy(expr->coordinate.y);
    copy->coordinate.z = expression_copy(expr->coordinate.z);
    if (!copy->coordinate.x || !copy->coordinate.y || !copy->coordinate.z) {
      expression_free(copy);
      return NULL;
    }
    break;
  case EXPR_FUNCTION_CALL:
    strncpy(copy->function_call.name, expr->function_call.name, MAX_TOKEN_VALUE_LENGTH);
    expression_list_reset(&copy->function_call.args);
    for (size_t i = 0; i < expr->function_call.args.count; i++) {
      Expression *arg_copy = expression_copy(expr->function_call.args.items[i]);
      if (!arg_copy || !expression_list_push(NULL, &copy->function_call.args, arg_copy)) {
        expression_free(arg_copy);
        expression_free(copy);
        return NULL;
      }
    }
    break;
  }

  return copy;
}

// Memory cleanup
void expression_free(Expression *expr) {
  if (!expr) return;

  switch (expr->type) {
  case EXPR_BINARY_OP:
    expression_free(expr->binary.left);
    expression_free(expr->binary.right);
    break;
  case EXPR_UNARY_OP: expression_free(expr->unary.operand); break;
  case EXPR_COORDINATE:
    expression_free(expr->coordinate.x);
    expression_free(expr->coordinate.y);
    expression_free(expr->coordinate.z);
    break;
  case EXPR_FUNCTION_CALL:
    for (size_t i = 0; i < expr->function_call.args.count; i++) {
      expression_free(expr->function_call.args.items[i]);
    }
    free(expr->function_call.args.items);
    break;
  default: break;
  }

  free(expr);
}

void instruction_free(Instruction *instr) {
  if (!instr) return;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: expression_free(instr->block_placement.coordinate); break;
  case INSTR_ASSIGNMENT: expression_free(instr->assignment.value); break;
  case INSTR_FOR_LOOP:
    expression_free(instr->for_loop.start);
    expression_free(instr->for_loop.end);
    for (size_t i = 0; i < instr->for_loop.body.count; i++) {
      instruction_free(instr->for_loop.body.items[i]);
    }
    free(instr->for_loop.body.items);
    break;
  case INSTR_IF_STATEMENT:
    for (size_t i = 0; i < instr->if_statement.branch_count; i++) {
      ConditionalBranch *branch = &instr->if_statement.branches[i];
      expression_free(branch->condition);
      for (size_t j = 0; j < branch->body.count; j++) {
        instruction_free(branch->body.items[j]);
      }
      free(branch->body.items);
    }
    free(instr->if_statement.branches);
    break;
  case INSTR_OCCURRENCE:
    for (size_t i = 0; i < instr->occurrence.args.count; i++) {
      expression_free(instr->occurrence.args.items[i].value);
    }
    free(instr->occurrence.args.items);
    expression_free(instr->occurrence.condition);
    for (size_t i = 0; i < instr->occurrence.body.count; i++) {
      instruction_free(instr->occurrence.body.items[i]);
    }
    free(instr->occurrence.body.items);
    break;
  case INSTR_PALETTE_DEFINITION: free(instr->palette_definition.entries.items); break;
  case INSTR_MACRO_CALL:
    for (size_t i = 0; i < instr->macro_call.arguments.count; i++) {
      expression_free(instr->macro_call.arguments.items[i].value);
    }
    free(instr->macro_call.arguments.items);
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
  registry->entries = NULL;
  registry->entry_capacity = 0;
  registry->entry_count = 0;
}

void macro_registry_register(MacroRegistry *registry, const char *name, MacroGenerator generator) {
  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return;
  }

  MacroRegistryEntry *entry = &registry->entries[registry->entry_count++];
  strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  entry->name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  entry->generator = generator;
}

MacroGenerator macro_registry_lookup(MacroRegistry *registry, const char *name) {
  for (size_t i = 0; i < registry->entry_count; i++) {
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

// Occurrence registry implementation
void occurrence_registry_init(OccurrenceRegistry *registry) {
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

static void occurrence_registry_entry_free(OccurrenceRegistryEntry *entry) {
  if (!entry) return;
  for (size_t i = 0; i < entry->args.count; i++) {
    expression_free(entry->args.items[i].value);
  }
  free(entry->args.items);
  entry->args.items = NULL;
  entry->args.count = 0;
  entry->args.capacity = 0;
}

void occurrence_registry_free(OccurrenceRegistry *registry) {
  if (!registry) return;
  for (size_t i = 0; i < registry->entry_count; i++) {
    occurrence_registry_entry_free(&registry->entries[i]);
  }
  free(registry->entries);
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

OccurrenceRegistryEntry *occurrence_registry_lookup(OccurrenceRegistry *registry, const char *name) {
  if (!registry || !name) return NULL;
  for (size_t i = 0; i < registry->entry_count; i++) {
    if (strcmp(registry->entries[i].name, name) == 0) {
      return &registry->entries[i];
    }
  }
  return NULL;
}

bool occurrence_registry_set(OccurrenceRegistry *registry, const char *name, const char *type, const NamedArgumentList *args) {
  if (!registry || !name || !type) return false;

  OccurrenceRegistryEntry *entry = occurrence_registry_lookup(registry, name);
  if (!entry) {
    if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
      return false;
    }

    entry = &registry->entries[registry->entry_count++];
    named_argument_list_reset(&entry->args);
    strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH - 1);
    entry->name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  } else {
    occurrence_registry_entry_free(entry);
    named_argument_list_reset(&entry->args);
  }

  strncpy(entry->type, type, MAX_TOKEN_VALUE_LENGTH - 1);
  entry->type[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';

  // Deep copy the arguments
  if (args && args->count > 0) {
    for (size_t i = 0; i < args->count; i++) {
      NamedArgument arg_copy = {0};
      strncpy(arg_copy.name, args->items[i].name, MAX_TOKEN_VALUE_LENGTH - 1);
      arg_copy.name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
      arg_copy.value = expression_copy(args->items[i].value);
      if (!arg_copy.value || !named_argument_list_push(NULL, &entry->args, arg_copy)) {
        expression_free(arg_copy.value);
        return false;
      }
    }
  }

  return true;
}

void occurrence_type_registry_init(OccurrenceTypeRegistry *registry) {
  if (!registry) return;
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

void occurrence_type_registry_free(OccurrenceTypeRegistry *registry) {
  if (!registry) return;
  free(registry->entries);
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

void occurrence_type_registry_register(OccurrenceTypeRegistry *registry, const char *name, OccurrenceGenerator generator) {
  if (!registry || !name || !generator) {
    return;
  }

  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return;
  }

  OccurrenceTypeRegistryEntry *entry = &registry->entries[registry->entry_count++];
  strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  entry->name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  entry->generator = generator;
}

OccurrenceGenerator occurrence_type_registry_lookup(OccurrenceTypeRegistry *registry, const char *name) {
  if (!registry || !name) {
    return NULL;
  }

  for (size_t i = 0; i < registry->entry_count; i++) {
    if (strcmp(registry->entries[i].name, name) == 0) {
      return registry->entries[i].generator;
    }
  }

  return NULL;
}

// Function registry implementation
void function_registry_init(FunctionRegistry *registry) {
  registry->entries = NULL;
  registry->entry_count = 0;
  registry->entry_capacity = 0;
}

bool function_registry_register(FunctionRegistry *registry, const char *name, size_t min_args, size_t max_args, BuiltinFunction function) {
  if (!registry || !name || !function) {
    return false;
  }

  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return false;
  }

  FunctionRegistryEntry *entry = &registry->entries[registry->entry_count++];
  strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  entry->name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  entry->min_args = min_args;
  entry->max_args = max_args;
  entry->function = function;
  return true;
}

const FunctionRegistryEntry *function_registry_lookup(const FunctionRegistry *registry, const char *name) {
  if (!registry || !name) {
    return NULL;
  }

  for (size_t i = 0; i < registry->entry_count; i++) {
    if (strcmp(registry->entries[i].name, name) == 0) {
      return &registry->entries[i];
    }
  }
  return NULL;
}

void function_registry_free(FunctionRegistry *registry) {
  if (!registry) return;
  free(registry->entries);
  registry->entries = NULL;
  registry->entry_capacity = 0;
  registry->entry_count = 0;
}

// Variable context implementation (moving from static to exported)
void context_init(VariableContext *ctx) {
  ctx->variables = NULL;
  ctx->variable_capacity = 0;
  ctx->variable_count = 0;
}

void context_free(VariableContext *ctx) {
  free(ctx->variables);
  ctx->variables = NULL;
  ctx->variable_capacity = 0;
  ctx->variable_count = 0;
}

void context_set(VariableContext *ctx, const char *name, double value) {
  // Check if variable already exists
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      ctx->variables[i].value = value;
      return;
    }
  }

  // Add new variable
  if (!ensure_capacity(NULL, (void **)&ctx->variables, &ctx->variable_capacity, ctx->variable_count, sizeof(*ctx->variables))) {
    return;
  }

  Variable *variable = &ctx->variables[ctx->variable_count++];
  strncpy(variable->name, name, MAX_TOKEN_VALUE_LENGTH - 1);
  variable->name[MAX_TOKEN_VALUE_LENGTH - 1] = '\0';
  variable->value = value;
}

double context_get(VariableContext *ctx, const char *name) {
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      return ctx->variables[i].value;
    }
  }
  return 0.0;
}

Expression *named_arg_get(const NamedArgumentList *args, const char *name) {
  if (!args) return NULL;
  for (size_t i = 0; i < args->count; i++) {
    if (strcmp(args->items[i].name, name) == 0) {
      return args->items[i].value;
    }
  }
  return NULL;
}

// Bounding box helpers for InstructionList
void instruction_list_init_bounds(InstructionList *list) {
  if (!list) return;
  list->has_bounds = true;
  list->min_x = list->max_x = 0;
  list->min_y = list->max_y = 0;
  list->min_z = list->max_z = 0;
}

void instruction_list_expand_bounds(InstructionList *list, int x, int y, int z) {
  if (!list) return;
  
  if (!list->has_bounds) {
    list->has_bounds = true;
    list->min_x = list->max_x = x;
    list->min_y = list->max_y = y;
    list->min_z = list->max_z = z;
  } else {
    if (x < list->min_x) list->min_x = x;
    if (x > list->max_x) list->max_x = x;
    if (y < list->min_y) list->min_y = y;
    if (y > list->max_y) list->max_y = y;
    if (z < list->min_z) list->min_z = z;
    if (z > list->max_z) list->max_z = z;
  }
}

void instruction_list_merge_bounds(InstructionList *dest, const InstructionList *src) {
  if (!dest || !src || !src->has_bounds) return;
  
  if (!dest->has_bounds) {
    dest->has_bounds = true;
    dest->min_x = src->min_x;
    dest->max_x = src->max_x;
    dest->min_y = src->min_y;
    dest->max_y = src->max_y;
    dest->min_z = src->min_z;
    dest->max_z = src->max_z;
  } else {
    if (src->min_x < dest->min_x) dest->min_x = src->min_x;
    if (src->max_x > dest->max_x) dest->max_x = src->max_x;
    if (src->min_y < dest->min_y) dest->min_y = src->min_y;
    if (src->max_y > dest->max_y) dest->max_y = src->max_y;
    if (src->min_z < dest->min_z) dest->min_z = src->min_z;
    if (src->max_z > dest->max_z) dest->max_z = src->max_z;
  }
}

bool instruction_list_intersects_section(const InstructionList *list, int section_x, int section_y, int section_z) {
  if (!list || !list->has_bounds) return false;
  
  // Convert section coordinates to world coordinates
  const int base_x = section_x * 16;
  const int base_y = section_y * 16;
  const int base_z = section_z * 16;
  const int max_x = base_x + 15;
  const int max_y = base_y + 15;
  const int max_z = base_z + 15;
  
  // Check if bounding boxes intersect
  return !(list->max_x < base_x || list->min_x > max_x ||
           list->max_y < base_y || list->min_y > max_y ||
           list->max_z < base_z || list->min_z > max_z);
}

// Helper to estimate bounds from an expression (conservative estimate for non-literals)
static void estimate_expression_bounds(const Expression *expr, int *min_val, int *max_val, int conservative_range) {
  if (!expr) {
    *min_val = *max_val = 0;
    return;
  }
  
  switch (expr->type) {
    case EXPR_NUMBER:
      *min_val = *max_val = (int)expr->number;
      break;
    case EXPR_IDENTIFIER:
    case EXPR_FUNCTION_CALL:
      // For unknowns, use conservative range
      *min_val = -conservative_range;
      *max_val = conservative_range;
      break;
    case EXPR_BINARY_OP: {
      int left_min, left_max, right_min, right_max;
      estimate_expression_bounds(expr->binary.left, &left_min, &left_max, conservative_range);
      estimate_expression_bounds(expr->binary.right, &right_min, &right_max, conservative_range);
      
      switch (expr->binary.op) {
        case OP_ADD:
          *min_val = left_min + right_min;
          *max_val = left_max + right_max;
          break;
        case OP_SUBTRACT:
          *min_val = left_min - right_max;
          *max_val = left_max - right_min;
          break;
        case OP_MULTIPLY:
          // Take the extremes
          *min_val = left_min * right_min;
          *max_val = left_max * right_max;
          if (left_min * right_max < *min_val) *min_val = left_min * right_max;
          if (left_max * right_min < *min_val) *min_val = left_max * right_min;
          if (left_min * right_max > *max_val) *max_val = left_min * right_max;
          if (left_max * right_min > *max_val) *max_val = left_max * right_min;
          break;
        default:
          // For comparisons and other ops, use conservative range
          *min_val = -conservative_range;
          *max_val = conservative_range;
          break;
      }
      break;
    }
    case EXPR_UNARY_OP:
      estimate_expression_bounds(expr->unary.operand, min_val, max_val, conservative_range);
      if (expr->unary.op == OP_NEGATE) {
        int tmp = *min_val;
        *min_val = -*max_val;
        *max_val = -tmp;
      }
      break;
    default:
      *min_val = -conservative_range;
      *max_val = conservative_range;
      break;
  }
}

// Update instruction list bounds based on an instruction
static void update_instruction_bounds(InstructionList *list, const Instruction *instr, int conservative_range) {
  if (!list || !instr) return;
  
  switch (instr->type) {
    case INSTR_BLOCK_PLACEMENT: {
      const BlockPlacement *placement = &instr->block_placement;
      if (placement->coordinate && placement->coordinate->type == EXPR_COORDINATE) {
        int x_min, x_max, y_min, y_max, z_min, z_max;
        estimate_expression_bounds(placement->coordinate->coordinate.x, &x_min, &x_max, conservative_range);
        
        if (placement->coordinate->coordinate.y) {
          estimate_expression_bounds(placement->coordinate->coordinate.y, &y_min, &y_max, conservative_range);
        } else {
          y_min = y_max = 0;
        }
        
        if (placement->coordinate->coordinate.z) {
          estimate_expression_bounds(placement->coordinate->coordinate.z, &z_min, &z_max, conservative_range);
        } else {
          z_min = z_max = 0;
        }
        
        instruction_list_expand_bounds(list, x_min, y_min, z_min);
        instruction_list_expand_bounds(list, x_max, y_max, z_max);
      }
      break;
    }
    case INSTR_FOR_LOOP: {
      const ForLoop *loop = &instr->for_loop;
      if (loop->body.has_bounds) {
        // Merge loop body bounds (potentially expanded by loop range)
        instruction_list_merge_bounds(list, &loop->body);
      }
      break;
    }
    case INSTR_IF_STATEMENT: {
      const IfStatement *if_stmt = &instr->if_statement;
      for (size_t i = 0; i < if_stmt->branch_count; i++) {
        if (if_stmt->branches[i].body.has_bounds) {
          instruction_list_merge_bounds(list, &if_stmt->branches[i].body);
        }
      }
      break;
    }
    case INSTR_OCCURRENCE: {
      const Occurrence *occ = &instr->occurrence;
      if (occ->body.has_bounds) {
        instruction_list_merge_bounds(list, &occ->body);
      }
      break;
    }
    default:
      break;
  }
}
