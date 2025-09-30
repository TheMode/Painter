#pragma once

#include "tokenizer.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct Instruction Instruction;
typedef struct Expression Expression;
typedef struct BlockPlacement BlockPlacement;
typedef struct Assignment Assignment;
typedef struct ForLoop ForLoop;
typedef struct Occurrence Occurrence;
typedef struct PaletteDefinition PaletteDefinition;
typedef struct PaletteEntry PaletteEntry;

// Expression types
typedef enum {
  EXPR_NUMBER,
  EXPR_IDENTIFIER,
  EXPR_BINARY_OP,
  EXPR_COORDINATE,
  EXPR_FUNCTION_CALL,
} ExpressionType;

typedef enum {
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_MODULO,
  OP_EQUAL,
  OP_NOT_EQUAL,
  OP_LESS,
  OP_LESS_EQUAL,
  OP_GREATER,
  OP_GREATER_EQUAL,
} BinaryOperator;

typedef struct Expression {
  ExpressionType type;
  union {
    double number;
    char identifier[MAX_TOKEN_VALUE_LENGTH];
    struct {
      BinaryOperator op;
      Expression *left;
      Expression *right;
    } binary;
    struct {
      Expression *x;
      Expression *y;
      Expression *z;
    } coordinate;
    struct {
      char name[MAX_TOKEN_VALUE_LENGTH];
      Expression **args;
      int arg_count;
    } function_call;
  };
} Expression;

// Block placement: [x y z] block_name[properties]
typedef struct BlockPlacement {
  Expression *coordinate;
  char block_name[MAX_TOKEN_VALUE_LENGTH];
  char block_properties[MAX_TOKEN_VALUE_LENGTH];
} BlockPlacement;

// Variable assignment: name = expression
typedef struct Assignment {
  char name[MAX_TOKEN_VALUE_LENGTH];
  Expression *value;
} Assignment;

// For loop: for var in start..end { instructions }
typedef struct ForLoop {
  char variable[MAX_TOKEN_VALUE_LENGTH];
  Expression *start;
  Expression *end;
  Instruction **body;
  int body_count;
} ForLoop;

// Occurrence: @type(args) [condition] { instructions }
typedef struct Occurrence {
  char type[MAX_TOKEN_VALUE_LENGTH];
  Expression **args;
  int arg_count;
  Expression *condition; // optional
  Instruction **body;
  int body_count;
} Occurrence;

// Palette entry: key: block_name[properties]
typedef struct PaletteEntry {
  int key;
  char block_name[MAX_TOKEN_VALUE_LENGTH];
  char block_properties[MAX_TOKEN_VALUE_LENGTH];
} PaletteEntry;

// Palette definition: name = { entries }
typedef struct PaletteDefinition {
  char name[MAX_TOKEN_VALUE_LENGTH];
  PaletteEntry *entries;
  int entry_count;
} PaletteDefinition;

// Instruction types
typedef enum {
  INSTR_BLOCK_PLACEMENT,
  INSTR_ASSIGNMENT,
  INSTR_FOR_LOOP,
  INSTR_OCCURRENCE,
  INSTR_PALETTE_DEFINITION,
} InstructionType;

typedef struct Instruction {
  InstructionType type;
  union {
    BlockPlacement block_placement;
    Assignment assignment;
    ForLoop for_loop;
    Occurrence occurrence;
    PaletteDefinition palette_definition;
  };
} Instruction;

// Parser context
typedef struct {
  Tokenizer tokenizer;
  Token current_token;
  bool has_error;
  char error_message[256];
} Parser;

// Main program structure
typedef struct {
  Instruction **instructions;
  int instruction_count;
  int instruction_capacity;
} Program;

// Parser API
void parser_init(Parser *parser, const char *input);
Program *parse_program(Parser *parser);
void program_free(Program *program);

// Helper functions
void parser_error(Parser *parser, const char *message);
void expression_free(Expression *expr);
void instruction_free(Instruction *instr);