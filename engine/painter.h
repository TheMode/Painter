#pragma once

#include "tokenizer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
typedef struct Instruction Instruction;
typedef struct Expression Expression;
typedef struct BlockPlacement BlockPlacement;
typedef struct Assignment Assignment;
typedef struct ForLoop ForLoop;
typedef struct Occurrence Occurrence;
typedef struct PaletteDefinition PaletteDefinition;
typedef struct PaletteEntry PaletteEntry;
typedef struct MacroCall MacroCall;
typedef struct MacroArgument MacroArgument;

// Expression types
typedef enum {
  EXPR_NUMBER,
  EXPR_IDENTIFIER,
  EXPR_BINARY_OP,
  EXPR_UNARY_OP,
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

typedef enum {
  OP_NEGATE,
} UnaryOperator;

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
      UnaryOperator op;
      Expression *operand;
    } unary;
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

// Macro argument: .name=value
typedef struct MacroArgument {
  char name[MAX_TOKEN_VALUE_LENGTH];
  Expression *value;
} MacroArgument;

// Macro call: #macro_name .arg1=val1 .arg2=val2
typedef struct MacroCall {
  char name[MAX_TOKEN_VALUE_LENGTH];
  MacroArgument *arguments;
  int argument_count;
} MacroCall;

// Instruction types
typedef enum {
  INSTR_BLOCK_PLACEMENT,
  INSTR_ASSIGNMENT,
  INSTR_FOR_LOOP,
  INSTR_OCCURRENCE,
  INSTR_PALETTE_DEFINITION,
  INSTR_MACRO_CALL,
} InstructionType;

typedef struct Instruction {
  InstructionType type;
  union {
    BlockPlacement block_placement;
    Assignment assignment;
    ForLoop for_loop;
    Occurrence occurrence;
    PaletteDefinition palette_definition;
    MacroCall macro_call;
  };
} Instruction;

// Parser context
typedef struct {
  Tokenizer tokenizer;
  bool has_error;
  char error_message[256];
} Parser;

// Main program structure
typedef struct {
  Instruction **instructions;
  int instruction_count;
  int instruction_capacity;
} Program;

// Section structure (16x16x16 Minecraft section)
typedef struct {
  char **palette;           // Array of block strings (e.g., "air", "oak_planks[facing=east]")
  int palette_size;         // Number of unique blocks in the palette
  int bits_per_entry;       // Bits needed to represent each block index
  uint64_t *data;           // Array of 64-bit integers holding block indices
  int data_size;            // Number of uint64_t elements in data array
} Section;

// Variable context for tracking values during execution
typedef struct Variable {
  char name[MAX_TOKEN_VALUE_LENGTH];
  double value;
} Variable;

typedef struct VariableContext {
  Variable *variables;
  int variable_count;
  int variable_capacity;
} VariableContext;

// Macro generator function type
// Takes: variable context, macro arguments, argument count, section info, block_indices, palette info
// Modifies block_indices array to place blocks
typedef void (*MacroGenerator)(VariableContext *ctx, MacroArgument *args, int arg_count,
                               int base_x, int base_y, int base_z,
                               int *block_indices, char ***palette, 
                               int *palette_size, int *palette_capacity);

// Macro registry entry
typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  MacroGenerator generator;
} MacroRegistryEntry;

// Macro registry
typedef struct {
  MacroRegistryEntry *entries;
  int entry_count;
  int entry_capacity;
} MacroRegistry;

// Parser API
void parser_init(Parser *parser, const char *input);
Program *parse_program(Parser *parser);
void program_free(Program *program);

// Section generation API
Section *generate_section(Program *program, int section_x, int section_y, int section_z);
void section_free(Section *section);

// Helper functions
void parser_error(Parser *parser, const char *message);
void expression_free(Expression *expr);
void instruction_free(Instruction *instr);

// Macro registry API
void macro_registry_init(MacroRegistry *registry);
void macro_registry_register(MacroRegistry *registry, const char *name, MacroGenerator generator);
MacroGenerator macro_registry_lookup(MacroRegistry *registry, const char *name);
void macro_registry_free(MacroRegistry *registry);

// Variable context API
void context_init(VariableContext *ctx);
void context_free(VariableContext *ctx);
void context_set(VariableContext *ctx, const char *name, double value);
double context_get(VariableContext *ctx, const char *name);

// Helper function to get macro argument by name
Expression *macro_get_arg(MacroArgument *args, int arg_count, const char *name);