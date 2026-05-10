#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "painter.h"
#include "builtin_functions.h"
#include "builtin_macros.h"
#include "builtin_occurrences.h"
#include "painter_eval.h"
#include <ctype.h>
#include <limits.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline void safe_strncpy(char *dst, const char *src, size_t dst_size) {
  if (!dst || dst_size == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= dst_size) len = dst_size - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Forward declaration for bounds tracking
static void update_instruction_bounds(InstructionList *list, const Instruction *instr, int conservative_range);

// Compile-time interval registry (thread-local, active only during parse_program).
typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  int min_val;
  int max_val;
} CompileBoundEntry;

typedef struct {
  CompileBoundEntry *entries;
  size_t count;
  size_t capacity;
} CompileBoundsTls;

static _Thread_local CompileBoundsTls g_compile_bounds;

static void compile_bounds_clear(void) {
  free(g_compile_bounds.entries);
  g_compile_bounds = (CompileBoundsTls){0};
}

static bool compile_bounds_ensure_capacity(void) {
  if (g_compile_bounds.count < g_compile_bounds.capacity) {
    return true;
  }
  size_t new_cap = g_compile_bounds.capacity ? g_compile_bounds.capacity * 2 : 16u;
  CompileBoundEntry *next = realloc(g_compile_bounds.entries, new_cap * sizeof(CompileBoundEntry));
  if (!next) {
    return false;
  }
  g_compile_bounds.entries = next;
  g_compile_bounds.capacity = new_cap;
  return true;
}

static bool compile_bounds_lookup(const char *name, int *min_v, int *max_v) {
  if (!name) {
    return false;
  }
  for (size_t i = 0; i < g_compile_bounds.count; i++) {
    if (strcmp(g_compile_bounds.entries[i].name, name) == 0) {
      *min_v = g_compile_bounds.entries[i].min_val;
      *max_v = g_compile_bounds.entries[i].max_val;
      return true;
    }
  }
  return false;
}

static void compile_bounds_set(const char *name, int min_v, int max_v) {
  if (!name || !name[0]) {
    return;
  }
  if (min_v > max_v) {
    int t = min_v;
    min_v = max_v;
    max_v = t;
  }
  for (size_t i = 0; i < g_compile_bounds.count; i++) {
    if (strcmp(g_compile_bounds.entries[i].name, name) == 0) {
      g_compile_bounds.entries[i].min_val = min_v;
      g_compile_bounds.entries[i].max_val = max_v;
      return;
    }
  }
  if (!compile_bounds_ensure_capacity()) {
    return;
  }
  CompileBoundEntry *e = &g_compile_bounds.entries[g_compile_bounds.count++];
  safe_strncpy(e->name, name, MAX_TOKEN_VALUE_LENGTH);
  e->min_val = min_v;
  e->max_val = max_v;
}

static void interval_round_bounds(double lo, double hi, int *omin, int *omax) {
  if (lo > hi) {
    double t = lo;
    lo = hi;
    hi = t;
  }
  int a = (int)floor(lo - 0.5);
  int b = (int)ceil(hi + 0.5);
  *omin = a < b ? a : b;
  *omax = a > b ? a : b;
}

static inline void bounds_unknown(int conservative_range, int *min_val, int *max_val) {
  *min_val = -conservative_range;
  *max_val = conservative_range;
}

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
    if (!compile_bounds_lookup(expr->identifier, min_val, max_val)) {
      bounds_unknown(conservative_range, min_val, max_val);
    }
    break;

  case EXPR_FUNCTION_CALL: {
    const char *fn = expr->function_call.name;
    const ExpressionList *args = &expr->function_call.args;
    if (strcmp(fn, "noise2d") == 0 || strcmp(fn, "noise3d") == 0) {
      *min_val = -1;
      *max_val = 1;
      break;
    }
    if (strcmp(fn, "round") == 0 && args->count >= 1) {
      int lo = 0, hi = 0;
      estimate_expression_bounds(args->items[0], &lo, &hi, conservative_range);
      interval_round_bounds((double)lo, (double)hi, min_val, max_val);
      break;
    }
    if (strcmp(fn, "floor") == 0 && args->count >= 1) {
      int lo = 0, hi = 0;
      estimate_expression_bounds(args->items[0], &lo, &hi, conservative_range);
      int fa = (int)floor((double)lo);
      int fb = (int)floor((double)hi);
      *min_val = fa < fb ? fa : fb;
      *max_val = fa > fb ? fa : fb;
      break;
    }
    if (strcmp(fn, "ceil") == 0 && args->count >= 1) {
      int lo = 0, hi = 0;
      estimate_expression_bounds(args->items[0], &lo, &hi, conservative_range);
      int ca = (int)ceil((double)lo);
      int cb = (int)ceil((double)hi);
      *min_val = ca < cb ? ca : cb;
      *max_val = ca > cb ? ca : cb;
      break;
    }
    if (strcmp(fn, "trunc") == 0 && args->count >= 1) {
      int lo = 0, hi = 0;
      estimate_expression_bounds(args->items[0], &lo, &hi, conservative_range);
      int ta = (int)lo;
      int tb = (int)hi;
      *min_val = ta < tb ? ta : tb;
      *max_val = ta > tb ? ta : tb;
      break;
    }
    bounds_unknown(conservative_range, min_val, max_val);
    break;
  }

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
      *min_val = left_min * right_min;
      *max_val = left_max * right_max;
      if (left_min * right_max < *min_val) *min_val = left_min * right_max;
      if (left_max * right_min < *min_val) *min_val = left_max * right_min;
      if (left_min * right_max > *max_val) *max_val = left_min * right_max;
      if (left_max * right_min > *max_val) *max_val = left_max * right_min;
      break;
    default:
      bounds_unknown(conservative_range, min_val, max_val);
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
    bounds_unknown(conservative_range, min_val, max_val);
    break;
  }
}

static void register_compile_bounds_for_assignment(const Assignment *assign) {
  if (!assign || assign->is_palette_definition || !assign->value) {
    return;
  }
  int lo = 0;
  int hi = 0;
  estimate_expression_bounds(assign->value, &lo, &hi, 100);
  compile_bounds_set(assign->name, lo, hi);
}

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

static void instruction_list_init(InstructionList *list) {
  if (list) *list = (InstructionList){0};
}

static void expression_list_init(ExpressionList *list) {
  if (list) *list = (ExpressionList){0};
}

static void named_argument_list_init(NamedArgumentList *list) {
  if (list) *list = (NamedArgumentList){0};
}

static void palette_entry_list_init(PaletteEntryList *list) {
  if (list) *list = (PaletteEntryList){0};
}

static void expression_list_destroy(ExpressionList *list) {
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) {
    expression_free(list->items[i]);
  }
  free(list->items);
  *list = (ExpressionList){0};
}

static void named_argument_list_destroy(NamedArgumentList *list) {
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) {
    expression_free(list->items[i].value);
  }
  free(list->items);
  *list = (NamedArgumentList){0};
}

static void palette_entry_list_destroy(PaletteEntryList *list) {
  if (!list) return;
  free(list->items);
  *list = (PaletteEntryList){0};
}

static bool palette_entry_list_copy(PaletteEntryList *dest, const PaletteEntryList *src) {
  if (!dest || !src) return false;
  palette_entry_list_destroy(dest);
  if (src->count == 0) return true;

  dest->items = malloc(sizeof(PaletteEntry) * src->count);
  if (!dest->items) return false;

  memcpy(dest->items, src->items, sizeof(PaletteEntry) * src->count);
  dest->count = dest->capacity = src->count;
  return true;
}

static PaletteDefinition *palette_definition_clone(const PaletteDefinition *definition) {
  if (!definition) return NULL;

  PaletteDefinition *copy = malloc(sizeof(*copy));
  if (!copy) return NULL;

  *copy = (PaletteDefinition){0};
  safe_strncpy(copy->name, definition->name, sizeof(copy->name));
  if (!palette_entry_list_copy(&copy->entries, &definition->entries)) {
    free(copy);
    return NULL;
  }
  return copy;
}

static void palette_definition_destroy(PaletteDefinition *definition) {
  if (!definition) return;
  palette_entry_list_destroy(&definition->entries);
  free(definition);
}

static void instruction_list_destroy(InstructionList *list) {
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) {
    instruction_free(list->items[i]);
  }
  free(list->items);
  *list = (InstructionList){0};
}

static void conditional_branch_destroy(ConditionalBranch *branch) {
  if (!branch) return;
  expression_free(branch->condition);
  instruction_list_destroy(&branch->body);
}

static void if_statement_destroy(IfStatement *if_stmt) {
  if (!if_stmt) return;
  for (size_t i = 0; i < if_stmt->branch_count; i++) {
    conditional_branch_destroy(&if_stmt->branches[i]);
  }
  free(if_stmt->branches);
  *if_stmt = (IfStatement){0};
}

static void occurrence_destroy(Occurrence *occurrence) {
  if (!occurrence) return;
  named_argument_list_destroy(&occurrence->args);
  expression_free(occurrence->condition);
  instruction_list_destroy(&occurrence->body);
}

static void macro_call_destroy(MacroCall *macro) {
  if (!macro) return;
  named_argument_list_destroy(&macro->arguments);
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
static Expression *parse_array_or_coordinate(Parser *parser);
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
static void program_build_palette_registry(Program *program);

// Parser initialization
void parser_init(Parser *parser, const char *input) {
  tokenizer_init(&parser->tokenizer, input);
  parser->has_error = false;
  parser->error_message[0] = '\0';
}

void parser_error(Parser *parser, const char *message) {
  if (!parser->has_error) {
    parser->has_error = true;
    // Include current line and column from the tokenizer to help locate parse errors
    int line = parser->tokenizer.line;
    int column = parser->tokenizer.column;
    snprintf(parser->error_message, sizeof(parser->error_message), "Parse error (line %d, col %d): %s", line, column, message);
    (void)0;
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
    safe_strncpy(expr->identifier, name, MAX_TOKEN_VALUE_LENGTH);
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

  // Handle array literals [expr, expr, ...]
  if (consume(TOKEN_LEFT_BRACKET)) {
    return parse_array_or_coordinate(parser);
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
    Token op_token = next();
    BinaryOperator op = (op_token.type == TOKEN_PLUS) ? OP_ADD : OP_SUBTRACT;
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
  // Returns an EXPR_ARRAY with 2 or 3 elements representing [x, z] or [x, y, z]
  // Each element can be a range expression (start..end)
  
  Expression *array = alloc_or_error(parser, sizeof(Expression), "Out of memory");
  if (!array) return NULL;
  
  array->type = EXPR_ARRAY;
  expression_list_init(&array->array.elements);

  // Parse first value (always x)
  Expression *first = parse_expression(parser);
  if (!first) goto error;

  // Check for range operator (..)
  if (consume(TOKEN_DOT_DOT)) {
    // Create array [start, end] for the range
    Expression *range_array = alloc_or_error(parser, sizeof(Expression), "Out of memory");
    if (!range_array) {
      expression_free(first);
      goto error;
    }
    range_array->type = EXPR_ARRAY;
    expression_list_init(&range_array->array.elements);
    
    if (!expression_list_push(parser, &range_array->array.elements, first)) {
      expression_free(first);
      expression_free(range_array);
      goto error;
    }
    
    Expression *first_end = parse_expression(parser);
    if (!first_end) {
      expression_free(range_array);
      goto error;
    }
    
    if (!expression_list_push(parser, &range_array->array.elements, first_end)) {
      expression_free(first_end);
      expression_free(range_array);
      goto error;
    }
    
    first = range_array;
  }
  
  if (!expression_list_push(parser, &array->array.elements, first)) {
    expression_free(first);
    goto error;
  }

  if (!expect_token(parser, TOKEN_COMMA, "Expected ',' between coordinate values")) {
    goto error;
  }

  // Parse second value (could be y or z depending on whether there's a third)
  Expression *second = parse_expression(parser);
  if (!second) goto error;

  // Check for range operator on second value
  if (consume(TOKEN_DOT_DOT)) {
    Expression *range_array = alloc_or_error(parser, sizeof(Expression), "Out of memory");
    if (!range_array) {
      expression_free(second);
      goto error;
    }
    range_array->type = EXPR_ARRAY;
    expression_list_init(&range_array->array.elements);
    
    if (!expression_list_push(parser, &range_array->array.elements, second)) {
      expression_free(second);
      expression_free(range_array);
      goto error;
    }
    
    Expression *second_end = parse_expression(parser);
    if (!second_end) {
      expression_free(range_array);
      goto error;
    }
    
    if (!expression_list_push(parser, &range_array->array.elements, second_end)) {
      expression_free(second_end);
      expression_free(range_array);
      goto error;
    }
    
    second = range_array;
  }

  if (consume(TOKEN_COMMA)) {
    // Three values provided: [x, y, z]
    // Add second as the y coordinate
    if (!expression_list_push(parser, &array->array.elements, second)) {
      expression_free(second);
      goto error;
    }
    
    // Parse z coordinate
    Expression *third = parse_expression(parser);
    if (!third) goto error;

    // Check for range operator on third value
    if (consume(TOKEN_DOT_DOT)) {
      Expression *range_array = alloc_or_error(parser, sizeof(Expression), "Out of memory");
      if (!range_array) {
        expression_free(third);
        goto error;
      }
      range_array->type = EXPR_ARRAY;
      expression_list_init(&range_array->array.elements);
      
      if (!expression_list_push(parser, &range_array->array.elements, third)) {
        expression_free(third);
        expression_free(range_array);
        goto error;
      }
      
      Expression *third_end = parse_expression(parser);
      if (!third_end) {
        expression_free(range_array);
        goto error;
      }
      
      if (!expression_list_push(parser, &range_array->array.elements, third_end)) {
        expression_free(third_end);
        expression_free(range_array);
        goto error;
      }
      
      third = range_array;
    }
    
    if (!expression_list_push(parser, &array->array.elements, third)) {
      expression_free(third);
      goto error;
    }
  } else {
    // Two values provided: [x, z], y defaults to 0
    // Insert a 0 for y coordinate
    Expression *zero_y = alloc_or_error(parser, sizeof(Expression), "Out of memory");
    if (!zero_y) {
      expression_free(second);
      goto error;
    }
    zero_y->type = EXPR_NUMBER;
    zero_y->number = 0.0;
    
    if (!expression_list_push(parser, &array->array.elements, zero_y)) {
      expression_free(zero_y);
      expression_free(second);
      goto error;
    }
    
    // Add second as the z coordinate
    if (!expression_list_push(parser, &array->array.elements, second)) {
      expression_free(second);
      goto error;
    }
  }

  if (!expect_token(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after coordinate")) {
    goto error;
  }

  return array;

error:
  expression_free(array);
  return NULL;
}

static Expression *parse_function_call(Parser *parser, const char *name) {
  Expression *expr = alloc_or_error(parser, sizeof(Expression), "Out of memory");
  if (!expr) return NULL;

  expr->type = EXPR_FUNCTION_CALL;
  safe_strncpy(expr->function_call.name, name, MAX_TOKEN_VALUE_LENGTH);
  expression_list_init(&expr->function_call.args);

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
}

// Parse array or coordinate based on context
// Arrays: [expr, expr, ...] - arbitrary number of elements
// Coordinates: [x, y?, z?] with optional ranges (..)
static Expression *parse_array_or_coordinate(Parser *parser) {
  // [ is already consumed
  // We need to look ahead to determine if this is an array or coordinate
  // Strategy: Parse as array first, check if it matches coordinate pattern
  
  Expression *array_expr = alloc_or_error(parser, sizeof(Expression), "Out of memory");
  if (!array_expr) return NULL;
  
  array_expr->type = EXPR_ARRAY;
  expression_list_init(&array_expr->array.elements);
  
  // Handle empty array []
  if (peek_type() == TOKEN_RIGHT_BRACKET) {
    consume(TOKEN_RIGHT_BRACKET);
    return array_expr;
  }
  
  // Parse elements
  while (true) {
    Expression *elem = parse_expression(parser);
    if (!elem) {
      expression_free(array_expr);
      return NULL;
    }
    
    if (!expression_list_push(parser, &array_expr->array.elements, elem)) {
      expression_free(elem);
      expression_free(array_expr);
      return NULL;
    }
    
    if (!consume(TOKEN_COMMA)) break;
  }
  
  if (!expect_token(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array elements")) {
    expression_free(array_expr);
    return NULL;
  }
  
  return array_expr;
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

  safe_strncpy(placement.block_name, name_token.value, MAX_TOKEN_VALUE_LENGTH);
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
      /* copy remaining namespace segment safely into the buffer */
      safe_strncpy(placement.block_name + current_len + 1, ns_token.value, MAX_TOKEN_VALUE_LENGTH - current_len - 1);
    }
    next();
  }

  // Parse optional block properties [key=value,...]
  placement.block_properties[0] = '\0';
  if (peek_type() == TOKEN_LEFT_BRACKET) {
    Token next_token = tokenizer_peek_next_token(&parser->tokenizer);
    // Only treat this as block properties if:
    // 1. It's an empty bracket: []
    // 2. It's an identifier followed by = (checked by peeking two tokens ahead)
    bool is_block_properties = false;
    if (next_token.type == TOKEN_RIGHT_BRACKET) {
      is_block_properties = true;
    } else if (next_token.type == TOKEN_IDENTIFIER) {
      // Peek at the token after the identifier to see if it's '='
      // We need to manually peek two tokens ahead
      Tokenizer saved_state = parser->tokenizer;
      tokenizer_next_token(&parser->tokenizer); // consume '['
      tokenizer_next_token(&parser->tokenizer); // consume identifier
      Token token_after_id = tokenizer_peek_token(&parser->tokenizer);
      parser->tokenizer = saved_state; // restore state

      if (token_after_id.type == TOKEN_EQUAL) {
        is_block_properties = true;
      }
    }

    if (is_block_properties) {
      next(); // consume '['

      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = next();
        size_t token_len = strlen(prop_token.value);

        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          safe_strncpy(placement.block_properties + offset, prop_token.value, MAX_TOKEN_VALUE_LENGTH - offset);
          offset += strlen(placement.block_properties + offset);
        }
      }
      if (!expect_token(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after block properties")) {
        parser_error(parser, "Expected ']' after block properties");
      }
    }
  }

  painter_format_block(
      placement.block_identifier,
      sizeof(placement.block_identifier),
      placement.block_name,
      placement.block_properties);

  return placement;
}

static ForLoop parse_for_loop(Parser *parser) {
  ForLoop loop = {0};
  instruction_list_init(&loop.body);

  // 'for' keyword is already consumed by caller

  Token var_token = peek();
  if (var_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected loop variable name");
    return loop;
  }

  safe_strncpy(loop.variable, var_token.value, MAX_TOKEN_VALUE_LENGTH);
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
    instruction_list_init(&branch->body);

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
  named_argument_list_init(&occurrence->args);
  occurrence->condition = NULL;

  // Parse arguments (.name or .name=value)
  while (consume(TOKEN_DOT)) {
    Token arg_name_token = peek();
    if (arg_name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected argument name after '.'");
      return false;
    }

    NamedArgument arg = {0};
    safe_strncpy(arg.name, arg_name_token.value, MAX_TOKEN_VALUE_LENGTH);
    next();

    if (consume(TOKEN_EQUAL)) {
      arg.value = parse_expression(parser);
      if (!arg.value) {
        return false;
      }
    } else {
      arg.value = make_number(1.0);
      if (!arg.value) {
        parser_error(parser, "Out of memory");
        return false;
      }
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
  instruction_list_init(&occurrence->body);

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
  named_argument_list_init(&occurrence.args);
  instruction_list_init(&occurrence.body);

  next(); // consume '@'

  Token type_token = peek();
  if (type_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  safe_strncpy(occurrence.type, type_token.value, MAX_TOKEN_VALUE_LENGTH);
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
  safe_strncpy(occurrence.name, name, MAX_TOKEN_VALUE_LENGTH);
  occurrence.condition = NULL;
  named_argument_list_init(&occurrence.args);
  instruction_list_init(&occurrence.body);

  if (!consume(TOKEN_AT)) {
    parser_error(parser, "Expected '@' for occurrence definition");
    return occurrence;
  }

  Token type_token = peek();
  if (type_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected occurrence type");
    return occurrence;
  }

  safe_strncpy(occurrence.type, type_token.value, MAX_TOKEN_VALUE_LENGTH);
  next();

  if (!parse_occurrence_header(parser, &occurrence)) {
    return occurrence;
  }

  return occurrence;
}

static Occurrence parse_occurrence_reference(Parser *parser, const char *name) {
  Occurrence occurrence = {0};
  occurrence.kind = OCCURRENCE_KIND_REFERENCE;
  safe_strncpy(occurrence.name, name, MAX_TOKEN_VALUE_LENGTH);
  occurrence.type[0] = '\0';
  occurrence.condition = NULL;
  named_argument_list_init(&occurrence.args);
  instruction_list_init(&occurrence.body);

  if (!parse_occurrence_body(parser, &occurrence)) {
    return occurrence;
  }

  return occurrence;
}

static PaletteDefinition parse_palette_definition(Parser *parser) {
  PaletteDefinition palette = {0};
  palette_entry_list_init(&palette.entries);

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

    safe_strncpy(entry.block_name, block_token.value, MAX_TOKEN_VALUE_LENGTH);
    next();

    // Parse optional properties
    entry.block_properties[0] = '\0';
    if (consume(TOKEN_LEFT_BRACKET)) {
      size_t offset = 0;
      while (peek_type() != TOKEN_RIGHT_BRACKET && peek_type() != TOKEN_EOF) {
        Token prop_token = next();
        size_t token_len = strlen(prop_token.value);

        if (offset + token_len < MAX_TOKEN_VALUE_LENGTH - 1) {
          safe_strncpy(entry.block_properties + offset, prop_token.value, MAX_TOKEN_VALUE_LENGTH - offset);
          offset += strlen(entry.block_properties + offset);
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
  named_argument_list_init(&macro.arguments);

  // Consume '#'
  next();

  // Parse macro name
  Token name_token = peek();
  if (name_token.type != TOKEN_IDENTIFIER) {
    parser_error(parser, "Expected macro name after '#'");
    return macro;
  }

  safe_strncpy(macro.name, name_token.value, MAX_TOKEN_VALUE_LENGTH);
  next();

  // Parse arguments (.name or .name=value)
  while (consume(TOKEN_DOT)) {
    Token arg_name_token = peek();
    if (arg_name_token.type != TOKEN_IDENTIFIER) {
      parser_error(parser, "Expected argument name after '.'");
      return macro;
    }

    NamedArgument arg = {0};
    safe_strncpy(arg.name, arg_name_token.value, MAX_TOKEN_VALUE_LENGTH);
    next();

    if (consume(TOKEN_EQUAL)) {
      arg.value = parse_expression(parser);
      if (!arg.value) {
        return macro;
      }
    } else {
      arg.value = make_number(1.0);
      if (!arg.value) {
        parser_error(parser, "Out of memory");
        return macro;
      }
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

  // Block placement: [x, y, z] block_name
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
    safe_strncpy(name, name_token.value, MAX_TOKEN_VALUE_LENGTH);

    if (consume(TOKEN_EQUAL)) {
      // Check if this is a palette definition or occurrence assignment
      if (peek_type() == TOKEN_LEFT_BRACE) {
        instr->type = INSTR_ASSIGNMENT;
        safe_strncpy(instr->assignment.name, name, MAX_TOKEN_VALUE_LENGTH);
        instr->assignment.is_palette_definition = true;
        PaletteDefinition tmp = parse_palette_definition(parser);
        PaletteDefinition *heap_def = malloc(sizeof(PaletteDefinition));
        if (!heap_def) {
          parser_error(parser, "Out of memory");
          free(instr);
          return NULL;
        }
        *heap_def = tmp;
        safe_strncpy(heap_def->name, name, MAX_TOKEN_VALUE_LENGTH);
        instr->assignment.palette_definition = heap_def;
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
        safe_strncpy(instr->assignment.name, name, MAX_TOKEN_VALUE_LENGTH);
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
  compile_bounds_clear();

  Program *program = malloc(sizeof(Program));
  if (!program) {
    parser_error(parser, "Out of memory");
    return NULL;
  }

  program->instructions = NULL;
  program->instruction_count = 0;
  program->instruction_capacity = 0;
  program->palette_registry = NULL;

  while (peek_type() != TOKEN_EOF && !parser->has_error) {
    Instruction *instr = parse_instruction(parser);
    if (instr) {
      if (instr->type == INSTR_ASSIGNMENT && !instr->assignment.is_palette_definition && instr->assignment.value) {
        register_compile_bounds_for_assignment(&instr->assignment);
      }
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

  if (!parser->has_error) {
    program->palette_registry = painter_palette_registry_create();
    if (program->palette_registry) {
      program_build_palette_registry(program);
    }
  }

  compile_bounds_clear();
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
  case EXPR_IDENTIFIER: safe_strncpy(copy->identifier, expr->identifier, MAX_TOKEN_VALUE_LENGTH); break;
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
  case EXPR_FUNCTION_CALL:
    safe_strncpy(copy->function_call.name, expr->function_call.name, MAX_TOKEN_VALUE_LENGTH);
    expression_list_init(&copy->function_call.args);
    for (size_t i = 0; i < expr->function_call.args.count; i++) {
      Expression *arg_copy = expression_copy(expr->function_call.args.items[i]);
      if (!arg_copy || !expression_list_push(NULL, &copy->function_call.args, arg_copy)) {
        expression_free(arg_copy);
        expression_free(copy);
        return NULL;
      }
    }
    break;
  case EXPR_ARRAY:
    expression_list_init(&copy->array.elements);
    for (size_t i = 0; i < expr->array.elements.count; i++) {
      Expression *elem_copy = expression_copy(expr->array.elements.items[i]);
      if (!elem_copy || !expression_list_push(NULL, &copy->array.elements, elem_copy)) {
        expression_free(elem_copy);
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
  case EXPR_FUNCTION_CALL: expression_list_destroy(&expr->function_call.args); break;
  case EXPR_ARRAY: expression_list_destroy(&expr->array.elements); break;
  default: break;
  }

  free(expr);
}

void instruction_free(Instruction *instr) {
  if (!instr) return;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: expression_free(instr->block_placement.coordinate); break;
  case INSTR_ASSIGNMENT:
    if (instr->assignment.is_palette_definition) {
      palette_definition_destroy(instr->assignment.palette_definition);
      instr->assignment.palette_definition = NULL;
    } else {
      expression_free(instr->assignment.value);
    }
    break;
  case INSTR_FOR_LOOP:
    expression_free(instr->for_loop.start);
    expression_free(instr->for_loop.end);
    instruction_list_destroy(&instr->for_loop.body);
    break;
  case INSTR_IF_STATEMENT: if_statement_destroy(&instr->if_statement); break;
  case INSTR_OCCURRENCE: occurrence_destroy(&instr->occurrence); break;
  case INSTR_MACRO_CALL: macro_call_destroy(&instr->macro_call); break;
  }

  free(instr);
}

void program_free(Program *program) {
  if (!program) return;

  for (int i = 0; i < program->instruction_count; i++) {
    instruction_free(program->instructions[i]);
  }
  free(program->instructions);
  painter_palette_registry_free(program->palette_registry);
  free(program);
}

// Macro registry implementation
void macro_registry_init(MacroRegistry *registry) {
  if (registry) *registry = (MacroRegistry){0};
}

void macro_registry_register(MacroRegistry *registry, const char *name, MacroGenerator generator) {
  if (!registry || !name || !generator) return;

  for (size_t i = 0; i < registry->entry_count; i++) {
    MacroRegistryEntry *existing = &registry->entries[i];
    if (strcmp(existing->name, name) == 0) {
      existing->generator = generator;
      return;
    }
  }

  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return;
  }

  MacroRegistryEntry *entry = &registry->entries[registry->entry_count++];
  safe_strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH);
  entry->generator = generator;
}

MacroGenerator macro_registry_lookup(MacroRegistry *registry, const char *name) {
  if (!registry || !name) return NULL;
  for (size_t i = 0; i < registry->entry_count; i++) {
    if (strcmp(registry->entries[i].name, name) == 0) {
      return registry->entries[i].generator;
    }
  }
  return NULL;
}

void macro_registry_free(MacroRegistry *registry) {
  if (!registry) return;
  free(registry->entries);
  *registry = (MacroRegistry){0};
}

// Occurrence registry implementation
void occurrence_registry_init(OccurrenceRegistry *registry) {
  if (registry) *registry = (OccurrenceRegistry){0};
}

static bool named_argument_list_clone(NamedArgumentList *dest, const NamedArgumentList *src) {
  if (!dest) return false;

  named_argument_list_destroy(dest);
  if (!src || src->count == 0) {
    return true;
  }

  NamedArgument *items = calloc(src->count, sizeof(NamedArgument));
  if (!items) {
    return false;
  }

  dest->items = items;
  dest->count = dest->capacity = src->count;

  for (size_t i = 0; i < src->count; i++) {
    NamedArgument *dest_arg = &dest->items[i];
    const NamedArgument *src_arg = &src->items[i];
    safe_strncpy(dest_arg->name, src_arg->name, MAX_TOKEN_VALUE_LENGTH);
    dest_arg->value = expression_copy(src_arg->value);
    if (!dest_arg->value) {
      for (size_t j = 0; j < i; j++) {
        expression_free(dest->items[j].value);
      }
      free(dest->items);
      *dest = (NamedArgumentList){0};
      return false;
    }
  }

  return true;
}

void occurrence_registry_free(OccurrenceRegistry *registry) {
  if (!registry) return;
  for (size_t i = 0; i < registry->entry_count; i++) {
    named_argument_list_destroy(&registry->entries[i].args);
  }
  free(registry->entries);
  *registry = (OccurrenceRegistry){0};
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
  } else {
    named_argument_list_destroy(&entry->args);
  }

  *entry = (OccurrenceRegistryEntry){0};
  safe_strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH);
  safe_strncpy(entry->type, type, MAX_TOKEN_VALUE_LENGTH);

  if (!named_argument_list_clone(&entry->args, args)) {
    return false;
  }

  return true;
}

void occurrence_type_registry_init(OccurrenceTypeRegistry *registry) {
  if (registry) *registry = (OccurrenceTypeRegistry){0};
}

void occurrence_type_registry_free(OccurrenceTypeRegistry *registry) {
  if (!registry) return;
  free(registry->entries);
  *registry = (OccurrenceTypeRegistry){0};
}

void occurrence_type_registry_register(OccurrenceTypeRegistry *registry, const char *name, OccurrenceGenerator generator) {
  if (!registry || !name || !generator) {
    return;
  }

  for (size_t i = 0; i < registry->entry_count; i++) {
    OccurrenceTypeRegistryEntry *existing = &registry->entries[i];
    if (strcmp(existing->name, name) == 0) {
      existing->generator = generator;
      return;
    }
  }

  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return;
  }

  OccurrenceTypeRegistryEntry *entry = &registry->entries[registry->entry_count++];
  safe_strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH);
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
  if (registry) *registry = (FunctionRegistry){0};
}

bool function_registry_register(FunctionRegistry *registry, const char *name, size_t min_args, size_t max_args, BuiltinFunction function) {
  if (!registry || !name || !function) {
    return false;
  }

  for (size_t i = 0; i < registry->entry_count; i++) {
    FunctionRegistryEntry *existing = &registry->entries[i];
    if (strcmp(existing->name, name) == 0) {
      existing->min_args = min_args;
      existing->max_args = max_args;
      existing->function = function;
      return true;
    }
  }

  if (!ensure_capacity(NULL, (void **)&registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries))) {
    return false;
  }

  FunctionRegistryEntry *entry = &registry->entries[registry->entry_count++];
  safe_strncpy(entry->name, name, MAX_TOKEN_VALUE_LENGTH);
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
  *registry = (FunctionRegistry){0};
}

// Variable context implementation (moving from static to exported)
void context_init(VariableContext *ctx) {
  ctx->variables = NULL;
  ctx->variable_capacity = 0;
  ctx->variable_count = 0;
}

void context_free(VariableContext *ctx) {
  if (!ctx) return;
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (ctx->variables[i].type == VAR_PALETTE && ctx->variables[i].v.palette) {
      palette_definition_destroy(ctx->variables[i].v.palette);
      ctx->variables[i].v.palette = NULL;
    } else if (ctx->variables[i].type == VAR_ARRAY && ctx->variables[i].v.array.items) {
      free(ctx->variables[i].v.array.items);
      ctx->variables[i].v.array.items = NULL;
    }
  }
  free(ctx->variables);
  ctx->variables = NULL;
  ctx->variable_capacity = 0;
  ctx->variable_count = 0;
}

void context_set(VariableContext *ctx, const char *name, double value) {
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      if (ctx->variables[i].type == VAR_PALETTE && ctx->variables[i].v.palette) {
        palette_definition_destroy(ctx->variables[i].v.palette);
      } else if (ctx->variables[i].type == VAR_ARRAY && ctx->variables[i].v.array.items) {
        free(ctx->variables[i].v.array.items);
      }
      ctx->variables[i].type = VAR_NUMBER;
      ctx->variables[i].v.value = value;
      return;
    }
  }

  if (!ensure_capacity(NULL, (void **)&ctx->variables, &ctx->variable_capacity, ctx->variable_count, sizeof(*ctx->variables))) {
    return;
  }

  Variable *variable = &ctx->variables[ctx->variable_count++];
  safe_strncpy(variable->name, name, MAX_TOKEN_VALUE_LENGTH);
  variable->type = VAR_NUMBER;
  variable->v.value = value;
}

void context_set_palette(VariableContext *ctx, const char *name, const PaletteDefinition *definition) {
  if (!ctx || !name || !definition) return;

  PaletteDefinition *copy = palette_definition_clone(definition);
  if (!copy) return;

  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      if (ctx->variables[i].type == VAR_PALETTE) {
        palette_definition_destroy(ctx->variables[i].v.palette);
      }
      ctx->variables[i].type = VAR_PALETTE;
      ctx->variables[i].v.palette = copy;
      return;
    }
  }

  if (!ensure_capacity(NULL, (void **)&ctx->variables, &ctx->variable_capacity, ctx->variable_count, sizeof(*ctx->variables))) {
    palette_definition_destroy(copy);
    return;
  }

  Variable *variable = &ctx->variables[ctx->variable_count++];
  safe_strncpy(variable->name, name, MAX_TOKEN_VALUE_LENGTH);
  variable->type = VAR_PALETTE;
  variable->v.palette = copy;
}

PaletteDefinition *context_get_palette(VariableContext *ctx, const char *name) {
  if (!ctx || !name) return NULL;
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0 && ctx->variables[i].type == VAR_PALETTE) {
      return ctx->variables[i].v.palette;
    }
  }
  return NULL;
}

void context_set_array(VariableContext *ctx, const char *name, const double *items, size_t count) {
  if (!ctx || !name) return;

  double *copy = NULL;
  if (count > 0 && items) {
    copy = malloc(sizeof(double) * count);
    if (!copy) return;
    memcpy(copy, items, sizeof(double) * count);
  }

  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      if (ctx->variables[i].type == VAR_PALETTE && ctx->variables[i].v.palette) {
        palette_definition_destroy(ctx->variables[i].v.palette);
      } else if (ctx->variables[i].type == VAR_ARRAY && ctx->variables[i].v.array.items) {
        free(ctx->variables[i].v.array.items);
      }
      ctx->variables[i].type = VAR_ARRAY;
      ctx->variables[i].v.array.items = copy;
      ctx->variables[i].v.array.count = count;
      return;
    }
  }

  if (!ensure_capacity(NULL, (void **)&ctx->variables, &ctx->variable_capacity, ctx->variable_count, sizeof(*ctx->variables))) {
    free(copy);
    return;
  }

  Variable *variable = &ctx->variables[ctx->variable_count++];
  safe_strncpy(variable->name, name, MAX_TOKEN_VALUE_LENGTH);
  variable->type = VAR_ARRAY;
  variable->v.array.items = copy;
  variable->v.array.count = count;
}

ArrayValue *context_get_array(VariableContext *ctx, const char *name) {
  if (!ctx || !name) return NULL;
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0 && ctx->variables[i].type == VAR_ARRAY) {
      return &ctx->variables[i].v.array;
    }
  }
  return NULL;
}

double context_get(VariableContext *ctx, const char *name) {
  if (!ctx || !name) return 0.0;
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) == 0) {
      if (ctx->variables[i].type == VAR_NUMBER) {
        return ctx->variables[i].v.value;
      } else {
        return 0.0;
      }
    }
  }
  return 0.0;
}

static void variable_context_remove_named(VariableContext *ctx, const char *name) {
  if (!ctx || !name) return;
  for (size_t i = 0; i < ctx->variable_count; i++) {
    if (strcmp(ctx->variables[i].name, name) != 0) {
      continue;
    }
    Variable *v = &ctx->variables[i];
    if (v->type == VAR_PALETTE && v->v.palette) {
      palette_definition_destroy(v->v.palette);
    } else if (v->type == VAR_ARRAY && v->v.array.items) {
      free(v->v.array.items);
    }
    size_t tail = ctx->variable_count - i - 1;
    if (tail > 0) {
      memmove(v, v + 1, tail * sizeof(Variable));
    }
    ctx->variable_count--;
    return;
  }
}

void context_strip_occurrence_axes(VariableContext *ctx) {
  variable_context_remove_named(ctx, "x");
  variable_context_remove_named(ctx, "y");
  variable_context_remove_named(ctx, "z");
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

// Palette registry implementation (thread-safe: lock-free reads, mutex-protected rare writes)
PaletteRegistry *painter_palette_registry_create(void) {
  PaletteRegistry *registry = calloc(1, sizeof(PaletteRegistry));
  if (!registry) return NULL;
  registry->capacity = 64;
  registry->strings = calloc(registry->capacity, sizeof(char *));
  if (!registry->strings) {
    free(registry);
    return NULL;
  }
#ifndef _WIN32
  registry->mutex = malloc(sizeof(pthread_mutex_t));
  if (registry->mutex) {
    pthread_mutex_t *mtx = (pthread_mutex_t *)registry->mutex;
    pthread_mutex_init(mtx, NULL);
  }
#endif
  return registry;
}

int painter_palette_registry_get_or_add(PaletteRegistry *registry, const char *block_string) {
  if (!registry || !block_string || !block_string[0]) return -1;

  // Fast path: lock-free read
  int count = registry->count;
  for (int i = 0; i < count; i++) {
    if (strcmp(registry->strings[i], block_string) == 0) {
      return i;
    }
  }

  // Slow path: acquire mutex for write
#ifndef _WIN32
  if (!registry->mutex) return -1;
  pthread_mutex_t *mtx = (pthread_mutex_t *)registry->mutex;
  pthread_mutex_lock(mtx);
#endif

  // Double-check after acquiring lock
  count = registry->count;
  for (int i = 0; i < count; i++) {
    if (strcmp(registry->strings[i], block_string) == 0) {
#ifndef _WIN32
      pthread_mutex_unlock(mtx);
#endif
      return i;
    }
  }

  // Grow if needed
  if (registry->count >= registry->capacity) {
    int new_capacity = registry->capacity * 2;
    char **resized = realloc(registry->strings, sizeof(char *) * new_capacity);
    if (!resized) {
#ifndef _WIN32
      pthread_mutex_unlock(mtx);
#endif
      return -1;
    }
    registry->strings = resized;
    registry->capacity = new_capacity;
  }

  size_t len = strlen(block_string);
  char *copy = malloc(len + 1);
  if (!copy) {
#ifndef _WIN32
    pthread_mutex_unlock(mtx);
#endif
    return -1;
  }
  memcpy(copy, block_string, len + 1);

  int id = registry->count;
  registry->strings[id] = copy;
  registry->count = id + 1;

#ifndef _WIN32
  pthread_mutex_unlock(mtx);
#endif
  return id;
}

const char *painter_palette_registry_get(const PaletteRegistry *registry, int id) {
  if (!registry || id < 0 || id >= registry->count) return NULL;
  return registry->strings[id];
}

void painter_palette_registry_free(PaletteRegistry *registry) {
  if (!registry) return;
  for (int i = 0; i < registry->count; i++) {
    free(registry->strings[i]);
  }
  free(registry->strings);
#ifndef _WIN32
  if (registry->mutex) {
    pthread_mutex_destroy((pthread_mutex_t *)registry->mutex);
    free(registry->mutex);
  }
#endif
  free(registry);
}

// Forward declarations for eager palette building
static void build_palette_from_instruction_list(const InstructionList *list, PaletteRegistry *registry);

static void build_palette_from_instruction(const Instruction *instr, PaletteRegistry *registry) {
  if (!instr || !registry) return;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: {
    const char *block_id = instr->block_placement.block_identifier;
    if (block_id[0]) {
      painter_palette_registry_get_or_add(registry, block_id);
    }
    break;
  }
  case INSTR_FOR_LOOP:
    build_palette_from_instruction_list(&instr->for_loop.body, registry);
    break;
  case INSTR_IF_STATEMENT:
    for (size_t i = 0; i < instr->if_statement.branch_count; i++) {
      build_palette_from_instruction_list(&instr->if_statement.branches[i].body, registry);
    }
    break;
  case INSTR_OCCURRENCE:
    build_palette_from_instruction_list(&instr->occurrence.body, registry);
    break;
  case INSTR_MACRO_CALL:
  case INSTR_ASSIGNMENT:
    break;
  }
}

static void build_palette_from_instruction_list(const InstructionList *list, PaletteRegistry *registry) {
  if (!list || !registry) return;
  for (size_t i = 0; i < list->count; i++) {
    build_palette_from_instruction(list->items[i], registry);
  }
}

void program_build_palette_registry(Program *program) {
  if (!program || !program->palette_registry) return;
  // Reserve ID 0 for "air" (always added first during generate_section)
  painter_palette_registry_get_or_add(program->palette_registry, "air");
  for (int i = 0; i < program->instruction_count; i++) {
    build_palette_from_instruction(program->instructions[i], program->palette_registry);
  }
}

// Bounding box helpers for InstructionList
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

// Update instruction list bounds based on an instruction
static void update_instruction_bounds(InstructionList *list, const Instruction *instr, int conservative_range) {
  if (!list || !instr) return;

  switch (instr->type) {
  case INSTR_ASSIGNMENT:
    if (!instr->assignment.is_palette_definition && instr->assignment.value) {
      register_compile_bounds_for_assignment(&instr->assignment);
    }
    break;
  case INSTR_BLOCK_PLACEMENT: {
    const BlockPlacement *placement = &instr->block_placement;
    if (placement->coordinate && placement->coordinate->type == EXPR_ARRAY) {
      const ExpressionList *elements = &placement->coordinate->array.elements;
      
      // Coordinate arrays must have exactly 3 elements: [x, y, z]
      if (elements->count != 3) {
        break;
      }
      
      Expression *x_expr = elements->items[0];
      Expression *y_expr = elements->items[1];
      Expression *z_expr = elements->items[2];
      
      int x_min, x_max, y_min, y_max, z_min, z_max;
      
      // Handle x coordinate (might be a range array [start, end])
      if (x_expr->type == EXPR_ARRAY && x_expr->array.elements.count == 2) {
        // It's a range
        int x_start_min, x_start_max, x_end_min, x_end_max;
        estimate_expression_bounds(x_expr->array.elements.items[0], &x_start_min, &x_start_max, conservative_range);
        estimate_expression_bounds(x_expr->array.elements.items[1], &x_end_min, &x_end_max, conservative_range);
        x_min = x_start_min < x_end_min ? x_start_min : x_end_min;
        x_max = x_start_max > x_end_max ? x_start_max : x_end_max;
      } else {
        estimate_expression_bounds(x_expr, &x_min, &x_max, conservative_range);
      }
      
      // Handle y coordinate (might be a range array [start, end])
      if (y_expr->type == EXPR_ARRAY && y_expr->array.elements.count == 2) {
        // It's a range
        int y_start_min, y_start_max, y_end_min, y_end_max;
        estimate_expression_bounds(y_expr->array.elements.items[0], &y_start_min, &y_start_max, conservative_range);
        estimate_expression_bounds(y_expr->array.elements.items[1], &y_end_min, &y_end_max, conservative_range);
        y_min = y_start_min < y_end_min ? y_start_min : y_end_min;
        y_max = y_start_max > y_end_max ? y_start_max : y_end_max;
      } else {
        estimate_expression_bounds(y_expr, &y_min, &y_max, conservative_range);
      }
      
      // Handle z coordinate (might be a range array [start, end])
      if (z_expr->type == EXPR_ARRAY && z_expr->array.elements.count == 2) {
        // It's a range
        int z_start_min, z_start_max, z_end_min, z_end_max;
        estimate_expression_bounds(z_expr->array.elements.items[0], &z_start_min, &z_start_max, conservative_range);
        estimate_expression_bounds(z_expr->array.elements.items[1], &z_end_min, &z_end_max, conservative_range);
        z_min = z_start_min < z_end_min ? z_start_min : z_end_min;
        z_max = z_start_max > z_end_max ? z_start_max : z_end_max;
      } else {
        estimate_expression_bounds(z_expr, &z_min, &z_max, conservative_range);
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
  default: break;
  }
}
