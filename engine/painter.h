#pragma once

#include "tokenizer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// DLL export/import macros for Windows
#ifdef _WIN32
#ifdef PAINTER_BUILD_DLL
#define PAINTER_API __declspec(dllexport)
#else
#define PAINTER_API __declspec(dllimport)
#endif
#else
#define PAINTER_API
#endif

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
typedef struct NamedArgument NamedArgument;
typedef struct FunctionRegistry FunctionRegistry;
typedef struct FunctionRegistryEntry FunctionRegistryEntry;
typedef struct ExecutionState ExecutionState;
typedef struct MacroRegistry MacroRegistry;
typedef struct OccurrenceRegistry OccurrenceRegistry;
typedef struct OccurrenceTypeRegistry OccurrenceTypeRegistry;
typedef struct VariableContext VariableContext;

// Generic pointer-backed containers used throughout the parser/executor
typedef struct {
  Expression **items;
  size_t count;
  size_t capacity;
} ExpressionList;

typedef struct {
  Instruction **items;
  size_t count;
  size_t capacity;
  // Bounding box for potential effects (in local coordinates)
  // Used to determine if this instruction list might affect neighboring sections
  bool has_bounds;
  int min_x, max_x;
  int min_y, max_y;
  int min_z, max_z;
} InstructionList;

typedef struct {
  PaletteEntry *items;
  size_t count;
  size_t capacity;
} PaletteEntryList;

typedef struct {
  NamedArgument *items;
  size_t count;
  size_t capacity;
} NamedArgumentList;

// Expression types
typedef enum {
  EXPR_NUMBER,
  EXPR_IDENTIFIER,
  EXPR_BINARY_OP,
  EXPR_UNARY_OP,
  EXPR_FUNCTION_CALL,
  EXPR_ARRAY,
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
      char name[MAX_TOKEN_VALUE_LENGTH];
      ExpressionList args;
    } function_call;
    struct {
      ExpressionList elements;
    } array;
  };
} Expression;

// Block placement: [x, y, z] block_name[properties]
typedef struct BlockPlacement {
  Expression *coordinate;
  char block_name[MAX_TOKEN_VALUE_LENGTH];
  char block_properties[MAX_TOKEN_VALUE_LENGTH];
  char block_identifier[MAX_TOKEN_VALUE_LENGTH * 2];
} BlockPlacement;

// Variable assignment: name = expression
typedef struct Assignment {
  char name[MAX_TOKEN_VALUE_LENGTH];
  Expression *value;
  bool is_palette_definition;
  PaletteDefinition *palette_definition;
} Assignment;

// For loop: for var in start..end { instructions }
typedef struct ForLoop {
  char variable[MAX_TOKEN_VALUE_LENGTH];
  Expression *start;
  Expression *end;
  InstructionList body;
} ForLoop;

// Occurrence instruction kinds
typedef enum {
  OCCURRENCE_KIND_IMMEDIATE,
  OCCURRENCE_KIND_DEFINITION,
  OCCURRENCE_KIND_REFERENCE,
} OccurrenceKind;

// Occurrence: @type .arg=value [condition] { instructions }
typedef struct Occurrence {
  OccurrenceKind kind;
  char name[MAX_TOKEN_VALUE_LENGTH];
  char type[MAX_TOKEN_VALUE_LENGTH];
  NamedArgumentList args;
  Expression *condition; // optional
  InstructionList body;
} Occurrence;

typedef struct OccurrenceRuntime OccurrenceRuntime;

typedef void (*OccurrenceGenerator)(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x,
    int origin_y, int origin_z, OccurrenceRuntime *runtime);

struct OccurrenceRuntime {
  int base_x;
  int base_y;
  int base_z;
  void (*run_body)(void *userdata, const InstructionList *body, int anchor_x, int anchor_y, int anchor_z);
  void *userdata;
};

// Palette entry: key: block_name[properties]
typedef struct PaletteEntry {
  int key;
  char block_name[MAX_TOKEN_VALUE_LENGTH];
  char block_properties[MAX_TOKEN_VALUE_LENGTH];
} PaletteEntry;

// Palette definition: name = { entries }
typedef struct PaletteDefinition {
  char name[MAX_TOKEN_VALUE_LENGTH];
  PaletteEntryList entries;
} PaletteDefinition;

// Named argument: .name=value
typedef struct NamedArgument {
  char name[MAX_TOKEN_VALUE_LENGTH];
  Expression *value;
} NamedArgument;

// Macro call: #macro_name .arg1=val1 .arg2=val2
typedef struct MacroCall {
  char name[MAX_TOKEN_VALUE_LENGTH];
  NamedArgumentList arguments;
} MacroCall;

// Conditional branch: if/elif with condition and body
typedef struct ConditionalBranch {
  Expression *condition; // NULL for else branch
  InstructionList body;
} ConditionalBranch;

// If statement: if(...) {...} elif(...) {...} else {...}
typedef struct IfStatement {
  ConditionalBranch *branches; // Array of if/elif/else branches
  size_t branch_count;
  size_t branch_capacity;
} IfStatement;

// Instruction types
typedef enum {
  INSTR_BLOCK_PLACEMENT,
  INSTR_ASSIGNMENT,
  INSTR_FOR_LOOP,
  INSTR_OCCURRENCE,
  INSTR_MACRO_CALL,
  INSTR_IF_STATEMENT,
} InstructionType;

typedef struct Instruction {
  InstructionType type;
  union {
    BlockPlacement block_placement;
    Assignment assignment;
    ForLoop for_loop;
    Occurrence occurrence;
    MacroCall macro_call;
    IfStatement if_statement;
  };
} Instruction;

// Parser context
typedef struct {
  Tokenizer tokenizer;
  bool has_error;
  char error_message[256];
} Parser;

// Main program structure (layout is consumed by Panama bindings; keep fields stable)
typedef struct {
  Instruction **instructions;
  int instruction_count;
  int instruction_capacity;
} Program;

// Section structure (16x16x16 Minecraft section)
typedef struct {
  char **palette;     // Array of block strings (e.g., "air", "oak_planks[facing=east]")
  int palette_size;   // Number of unique blocks in the palette
  int bits_per_entry; // Bits needed to represent each block index
  uint64_t *data;     // Array of 64-bit integers holding block indices
  int data_size;      // Number of uint64_t elements in data array
} Section;

// Array value storage
typedef struct ArrayValue {
  double *items;
  size_t count;
} ArrayValue;

// Variable context for tracking values during execution
typedef struct Variable {
  char name[MAX_TOKEN_VALUE_LENGTH];
  enum {
    VAR_NUMBER,
    VAR_PALETTE,
    VAR_ARRAY,
  } type;
  union {
    double value;
    PaletteDefinition *palette;
    ArrayValue array;
  } v;
} Variable;

struct VariableContext {
  Variable *variables;
  size_t variable_count;
  size_t variable_capacity;
};

// Occurrence registry entry used at execution time
typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  char type[MAX_TOKEN_VALUE_LENGTH];
  NamedArgumentList args;
} OccurrenceRegistryEntry;

struct OccurrenceRegistry {
  OccurrenceRegistryEntry *entries;
  size_t entry_count;
  size_t entry_capacity;
};

typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  OccurrenceGenerator generator;
} OccurrenceTypeRegistryEntry;

struct OccurrenceTypeRegistry {
  OccurrenceTypeRegistryEntry *entries;
  size_t entry_count;
  size_t entry_capacity;
};

// Built-in function signature (returns a double result for evaluated arguments)
typedef double (*BuiltinFunction)(const double *args, size_t arg_count);

// Function registry entry (tracks argument bounds for validation)
struct FunctionRegistryEntry {
  char name[MAX_TOKEN_VALUE_LENGTH];
  size_t min_args;
  size_t max_args;
  BuiltinFunction function;
};

struct FunctionRegistry {
  FunctionRegistryEntry *entries;
  size_t entry_count;
  size_t entry_capacity;
};

// Execution state that holds all runtime context for code generation
struct ExecutionState {
  VariableContext *variables;
  MacroRegistry *macros;
  FunctionRegistry *functions;
  OccurrenceRegistry *occurrences;
  OccurrenceTypeRegistry *occurrence_types;
  int base_x;
  int base_y;
  int base_z;
  int current_origin_x;
  int current_origin_y;
  int current_origin_z;
  int *block_indices;
  char ***palette;
  int *palette_size;
  int *palette_capacity;
};

// Axis-aligned bounding box helper for world coordinates
typedef struct {
  int min_x;
  int max_x;
  int min_y;
  int max_y;
  int min_z;
  int max_z;
} PainterAABB;

// Macro generator function type
// Modifies block_indices array to place blocks
typedef void (*MacroGenerator)(ExecutionState *state, const NamedArgumentList *args);

// Macro registry entry
typedef struct {
  char name[MAX_TOKEN_VALUE_LENGTH];
  MacroGenerator generator;
} MacroRegistryEntry;

// Macro registry
struct MacroRegistry {
  MacroRegistryEntry *entries;
  size_t entry_count;
  size_t entry_capacity;
};

// Parser API
PAINTER_API void parser_init(Parser *parser, const char *input);
PAINTER_API Program *parse_program(Parser *parser);
PAINTER_API void program_free(Program *program);

// Section generation API
PAINTER_API Section *generate_section(Program *program, int section_x, int section_y, int section_z);
PAINTER_API void section_free(Section *section);

// Helper functions
PAINTER_API void parser_error(Parser *parser, const char *message);
PAINTER_API void expression_free(Expression *expr);
PAINTER_API void instruction_free(Instruction *instr);

// Macro registry API
PAINTER_API void macro_registry_init(MacroRegistry *registry);
PAINTER_API void macro_registry_register(MacroRegistry *registry, const char *name, MacroGenerator generator);
PAINTER_API MacroGenerator macro_registry_lookup(MacroRegistry *registry, const char *name);

// Occurrence registry helpers
PAINTER_API void occurrence_registry_init(OccurrenceRegistry *registry);
PAINTER_API void occurrence_registry_free(OccurrenceRegistry *registry);
PAINTER_API OccurrenceRegistryEntry *occurrence_registry_lookup(OccurrenceRegistry *registry, const char *name);
PAINTER_API bool occurrence_registry_set(OccurrenceRegistry *registry, const char *name, const char *type, const NamedArgumentList *args);

PAINTER_API void occurrence_type_registry_init(OccurrenceTypeRegistry *registry);
PAINTER_API void occurrence_type_registry_free(OccurrenceTypeRegistry *registry);
PAINTER_API void occurrence_type_registry_register(OccurrenceTypeRegistry *registry, const char *name, OccurrenceGenerator generator);
PAINTER_API OccurrenceGenerator occurrence_type_registry_lookup(OccurrenceTypeRegistry *registry, const char *name);
PAINTER_API void macro_registry_free(MacroRegistry *registry);

// Function registry API
PAINTER_API void function_registry_init(FunctionRegistry *registry);
PAINTER_API bool function_registry_register(FunctionRegistry *registry, const char *name, size_t min_args, size_t max_args, BuiltinFunction function);
PAINTER_API const FunctionRegistryEntry *function_registry_lookup(const FunctionRegistry *registry, const char *name);
PAINTER_API void function_registry_free(FunctionRegistry *registry);

// Variable context API
PAINTER_API void context_init(VariableContext *ctx);
PAINTER_API void context_free(VariableContext *ctx);
PAINTER_API void context_set(VariableContext *ctx, const char *name, double value);
PAINTER_API double context_get(VariableContext *ctx, const char *name);
PAINTER_API void context_set_palette(VariableContext *ctx, const char *name, const PaletteDefinition *definition);
PAINTER_API PaletteDefinition *context_get_palette(VariableContext *ctx, const char *name);
PAINTER_API void context_set_array(VariableContext *ctx, const char *name, const double *items, size_t count);
PAINTER_API ArrayValue *context_get_array(VariableContext *ctx, const char *name);

// Helper function to get named argument by name
PAINTER_API Expression *named_arg_get(const NamedArgumentList *args, const char *name);

// Bounding box helpers for InstructionList
PAINTER_API void instruction_list_init_bounds(InstructionList *list);
PAINTER_API void instruction_list_expand_bounds(InstructionList *list, int x, int y, int z);
PAINTER_API void instruction_list_merge_bounds(InstructionList *dest, const InstructionList *src);
PAINTER_API bool instruction_list_intersects_section(const InstructionList *list, int section_x, int section_y, int section_z);
