#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "painter_eval.h"
#include "builtin_functions.h"
#include "builtin_macros.h"
#include "builtin_occurrences.h"
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for internal functions
static void execute_occurrence_instruction(const Occurrence *occurrence, ExecutionState *state, int origin_x, int origin_y, int origin_z);
static bool instruction_might_affect_section(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z);
static bool evaluate_block_position(const BlockPlacement *placement, ExecutionState *state, int origin_x, int origin_y, int origin_z,
    int *world_x, int *world_y, int *world_z);
static int calculate_bits_per_entry(int palette_size);
static void execute_occurrence_by_type(const char *type, const NamedArgumentList *args, const InstructionList *body, ExecutionState *state,
    int origin_x, int origin_y, int origin_z);
static void occurrence_runtime_run_body(void *userdata, const InstructionList *body, int anchor_x, int anchor_y, int anchor_z);
static void run_instruction_list(const InstructionList *list, ExecutionState *state, int origin_x, int origin_y, int origin_z);
static void
process_program_at_origin(Program *program, ExecutionState *state, int origin_x, int origin_y, int origin_z, bool occurrences_only);

// Calculate bits needed to represent palette indices
static int calculate_bits_per_entry(int palette_size) {
  static int const MIN_BITS = 4;
  if (palette_size <= 1) return MIN_BITS;
  unsigned x = (unsigned)(palette_size - 1);

#if defined(__GNUC__) || defined(__clang__)
  int bits = sizeof(unsigned) * CHAR_BIT - __builtin_clz(x);
#else
  int bits = 1;
  if (x >= 1u << 16) {
    bits += 16;
    x >>= 16;
  }
  if (x >= 1u << 8) {
    bits += 8;
    x >>= 8;
  }
  if (x >= 1u << 4) {
    bits += 4;
    x >>= 4;
  }
  if (x >= 1u << 2) {
    bits += 2;
    x >>= 2;
  }
  if (x >= 1u << 1) bits++;
#endif
  return bits < MIN_BITS ? MIN_BITS : bits;
}

// Evaluate block position (inlined for performance)
static inline bool evaluate_block_position(const BlockPlacement *placement, ExecutionState *state, int origin_x, int origin_y, int origin_z,
    int *world_x, int *world_y, int *world_z) {
  if (!placement->coordinate || placement->coordinate->type != EXPR_ARRAY) {
    return false;
  }

  const ExpressionList *elements = &placement->coordinate->array.elements;
  if (elements->count != 3) {
    return false; // Must be [x, y, z]
  }

  const int offset_x = (int)painter_evaluate_expression(elements->items[0], state);
  const int offset_y = (int)painter_evaluate_expression(elements->items[1], state);
  const int offset_z = (int)painter_evaluate_expression(elements->items[2], state);

  *world_x = origin_x + offset_x;
  *world_y = origin_y + offset_y;
  *world_z = origin_z + offset_z;
  return true;
}

// Evaluate an expression and return its numeric value
double painter_evaluate_expression(const Expression *expr, ExecutionState *state) {
  if (!expr || !state) return 0.0;

  switch (expr->type) {
  case EXPR_NUMBER: return expr->number;
  case EXPR_IDENTIFIER: return context_get(state->variables, expr->identifier);
  case EXPR_BINARY_OP: {
    double left = painter_evaluate_expression(expr->binary.left, state);
    double right = painter_evaluate_expression(expr->binary.right, state);
    switch (expr->binary.op) {
    case OP_ADD: return left + right;
    case OP_SUBTRACT: return left - right;
    case OP_MULTIPLY: return left * right;
    case OP_DIVIDE: return (right != 0.0) ? left / right : 0.0;
    case OP_MODULO: return (int)left % (int)right;
    case OP_EQUAL: return left == right;
    case OP_NOT_EQUAL: return left != right;
    case OP_LESS: return left < right;
    case OP_LESS_EQUAL: return left <= right;
    case OP_GREATER: return left > right;
    case OP_GREATER_EQUAL: return left >= right;
    }
    break;
  }
  case EXPR_UNARY_OP: {
    double operand = painter_evaluate_expression(expr->unary.operand, state);
    switch (expr->unary.op) {
    case OP_NEGATE: return -operand;
    }
    break;
  }
  case EXPR_FUNCTION_CALL: {
    if (!state->functions) {
      return 0.0;
    }

    const FunctionRegistryEntry *entry = function_registry_lookup(state->functions, expr->function_call.name);
    if (!entry) {
      return 0.0;
    }

    size_t arg_count = expr->function_call.args.count;
    if (arg_count < entry->min_args) {
      return 0.0;
    }
    if (entry->max_args != SIZE_MAX && arg_count > entry->max_args) {
      return 0.0;
    }

    if (arg_count == 0) {
      return entry->function ? entry->function(NULL, 0) : 0.0;
    }

    double arg_values[arg_count];
    for (size_t i = 0; i < arg_count; i++) {
      arg_values[i] = painter_evaluate_expression(expr->function_call.args.items[i], state);
    }
    return entry->function ? entry->function(arg_values, arg_count) : 0.0;
  }
  case EXPR_ARRAY: break; // Arrays don't evaluate to a single number
  }

  return 0.0;
}

// Check if instruction might affect section (optimized)
static bool instruction_might_affect_section(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z) {
  if (!instr) return false;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: {
    const BlockPlacement *placement = &instr->block_placement;
    const Expression *coord = placement->coordinate;

    if (!coord || coord->type != EXPR_ARRAY || coord->array.elements.count != 3) {
      return false;
    }

    Expression *x_expr = coord->array.elements.items[0];
    Expression *y_expr = coord->array.elements.items[1];
    Expression *z_expr = coord->array.elements.items[2];

    // Evaluate base coordinates
    int base_x, base_y, base_z;
    int end_x, end_y, end_z;

    // Handle x (might be a range [start, end])
    if (x_expr->type == EXPR_ARRAY && x_expr->array.elements.count == 2) {
      base_x = (int)painter_evaluate_expression(x_expr->array.elements.items[0], state);
      end_x = (int)painter_evaluate_expression(x_expr->array.elements.items[1], state);
    } else {
      base_x = (int)painter_evaluate_expression(x_expr, state);
      end_x = base_x;
    }

    // Handle y (might be a range [start, end])
    if (y_expr->type == EXPR_ARRAY && y_expr->array.elements.count == 2) {
      base_y = (int)painter_evaluate_expression(y_expr->array.elements.items[0], state);
      end_y = (int)painter_evaluate_expression(y_expr->array.elements.items[1], state);
    } else {
      base_y = (int)painter_evaluate_expression(y_expr, state);
      end_y = base_y;
    }

    // Handle z (might be a range [start, end])
    if (z_expr->type == EXPR_ARRAY && z_expr->array.elements.count == 2) {
      base_z = (int)painter_evaluate_expression(z_expr->array.elements.items[0], state);
      end_z = (int)painter_evaluate_expression(z_expr->array.elements.items[1], state);
    } else {
      base_z = (int)painter_evaluate_expression(z_expr, state);
      end_z = base_z;
    }

    // Calculate world-space bounding box
    const int world_min_x = origin_x + (base_x < end_x ? base_x : end_x);
    const int world_max_x = origin_x + (base_x > end_x ? base_x : end_x);
    const int world_min_y = origin_y + (base_y < end_y ? base_y : end_y);
    const int world_max_y = origin_y + (base_y > end_y ? base_y : end_y);
    const int world_min_z = origin_z + (base_z < end_z ? base_z : end_z);
    const int world_max_z = origin_z + (base_z > end_z ? base_z : end_z);

    const int sec_min_x = state->base_x;
    const int sec_max_x = state->base_x + 15;
    const int sec_min_y = state->base_y;
    const int sec_max_y = state->base_y + 15;
    const int sec_min_z = state->base_z;
    const int sec_max_z = state->base_z + 15;

    // Optimized AABB intersection test
    return !(world_max_x < sec_min_x || world_min_x > sec_max_x || world_max_y < sec_min_y || world_min_y > sec_max_y ||
             world_max_z < sec_min_z || world_min_z > sec_max_z);
  }
  case INSTR_OCCURRENCE: {
    const Occurrence *occ = &instr->occurrence;
    if (!occ->body.has_bounds) {
      return true; // Conservative: no bounds info, assume it might affect
    }

    // Calculate world-space bounds and check AABB intersection
    const int world_min_x = state->base_x + origin_x + occ->body.min_x;
    const int world_max_x = state->base_x + origin_x + occ->body.max_x;
    const int world_min_y = state->base_y + origin_y + occ->body.min_y;
    const int world_max_y = state->base_y + origin_y + occ->body.max_y;
    const int world_min_z = state->base_z + origin_z + occ->body.min_z;
    const int world_max_z = state->base_z + origin_z + occ->body.max_z;

    const int sec_min_x = state->base_x;
    const int sec_max_x = state->base_x + 15;
    const int sec_min_y = state->base_y;
    const int sec_max_y = state->base_y + 15;
    const int sec_min_z = state->base_z;
    const int sec_max_z = state->base_z + 15;

    // Optimized AABB intersection test
    return !(world_max_x < sec_min_x || world_min_x > sec_max_x || world_max_y < sec_min_y || world_min_y > sec_max_y ||
             world_max_z < sec_min_z || world_min_z > sec_max_z);
  }
  default: return true;
  }
}

// Occurrence runtime callback
static void run_instruction_list(const InstructionList *list, ExecutionState *state, int origin_x, int origin_y, int origin_z) {
  if (!list || !state) return;

  for (size_t i = 0; i < list->count; i++) {
    Instruction *child = list->items[i];
    if (!instruction_might_affect_section(child, state, origin_x, origin_y, origin_z)) {
      continue;
    }
    process_instruction(child, state, origin_x, origin_y, origin_z);
  }
}

static void occurrence_runtime_run_body(void *userdata, const InstructionList *body, int anchor_x, int anchor_y, int anchor_z) {
  ExecutionState *state = (ExecutionState *)userdata;
  if (!state || !body) return;

  // Inject x, y, z variables so they can be used in expressions like noise2d(x, z, ...)
  context_set(state->variables, "x", (double)anchor_x);
  context_set(state->variables, "y", (double)anchor_y);
  context_set(state->variables, "z", (double)anchor_z);

  run_instruction_list(body, state, anchor_x, anchor_y, anchor_z);
}

// Execute occurrence by type
static void execute_occurrence_by_type(const char *type, const NamedArgumentList *args, const InstructionList *body, ExecutionState *state,
    int origin_x, int origin_y, int origin_z) {
  if (!type || !state || !state->occurrence_types) {
    return;
  }

  OccurrenceGenerator generator = occurrence_type_registry_lookup(state->occurrence_types, type);
  if (!generator) {
    return;
  }

  OccurrenceRuntime runtime = {
      /* The runtime base coordinates should reflect the world-base of the
     * section/origin the occurrence is being executed for. Many occurrence
     * generators compute ranges relative to runtime->base and also receive
     * an origin offset parameter; when processing occurrences coming from
     * neighboring sections the origin parameters are non-zero, so we include
     * origin_* to state->base_*. This ensures occurrences run with the
     * correct world coordinates for the given origin. */
      .base_x = state->base_x + origin_x,
      .base_y = state->base_y + origin_y,
      .base_z = state->base_z + origin_z,
      .run_body = occurrence_runtime_run_body,
      .userdata = state,
  };

  generator(state, args, body, origin_x, origin_y, origin_z, &runtime);
}

// Execute occurrence instruction
static void execute_occurrence_instruction(const Occurrence *occurrence, ExecutionState *state, int origin_x, int origin_y, int origin_z) {
  if (!occurrence || !state) return;

  switch (occurrence->kind) {
  case OCCURRENCE_KIND_DEFINITION: {
    occurrence_registry_set(state->occurrences, occurrence->name, occurrence->type, &occurrence->args);
    break;
  }

  case OCCURRENCE_KIND_REFERENCE: {
    OccurrenceRegistryEntry *entry = occurrence_registry_lookup(state->occurrences, occurrence->name);
    if (!entry) {
      return;
    }
    execute_occurrence_by_type(entry->type, &entry->args, &occurrence->body, state, origin_x, origin_y, origin_z);
    break;
  }

  case OCCURRENCE_KIND_IMMEDIATE: {
    execute_occurrence_by_type(occurrence->type, &occurrence->args, &occurrence->body, state, origin_x, origin_y, origin_z);
    break;
  }
  }
}

// Process a single instruction at the given origin coordinates
void process_instruction(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z) {
  if (!instr || !state) return;

  state->current_origin_x = origin_x;
  state->current_origin_y = origin_y;
  state->current_origin_z = origin_z;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: {
    const BlockPlacement *placement = &instr->block_placement;
    const Expression *coord = placement->coordinate;

    if (!coord || coord->type != EXPR_ARRAY || coord->array.elements.count != 3) {
      break;
    }

    const char *block_string = placement->block_identifier;
    if (!block_string[0]) {
      break;
    }

    Expression *x_expr = coord->array.elements.items[0];
    Expression *y_expr = coord->array.elements.items[1];
    Expression *z_expr = coord->array.elements.items[2];

    // Evaluate base coordinates and range ends
    int base_x, end_x, base_y, end_y, base_z, end_z;

    // Handle x (might be a range [start, end])
    if (x_expr->type == EXPR_ARRAY && x_expr->array.elements.count == 2) {
      base_x = (int)painter_evaluate_expression(x_expr->array.elements.items[0], state);
      end_x = (int)painter_evaluate_expression(x_expr->array.elements.items[1], state);
    } else {
      base_x = (int)painter_evaluate_expression(x_expr, state);
      end_x = base_x;
    }

    // Handle y (might be a range [start, end])
    if (y_expr->type == EXPR_ARRAY && y_expr->array.elements.count == 2) {
      base_y = (int)painter_evaluate_expression(y_expr->array.elements.items[0], state);
      end_y = (int)painter_evaluate_expression(y_expr->array.elements.items[1], state);
    } else {
      base_y = (int)painter_evaluate_expression(y_expr, state);
      end_y = base_y;
    }

    // Handle z (might be a range [start, end])
    if (z_expr->type == EXPR_ARRAY && z_expr->array.elements.count == 2) {
      base_z = (int)painter_evaluate_expression(z_expr->array.elements.items[0], state);
      end_z = (int)painter_evaluate_expression(z_expr->array.elements.items[1], state);
    } else {
      base_z = (int)painter_evaluate_expression(z_expr, state);
      end_z = base_z;
    }

    // Determine iteration direction for each axis
    const int step_x = (end_x >= base_x) ? 1 : -1;
    const int step_y = (end_y >= base_y) ? 1 : -1;
    const int step_z = (end_z >= base_z) ? 1 : -1;

    // Get or add palette index once
    const int palette_index = painter_palette_get_or_add(state, block_string);
    if (palette_index < 0) {
      break;
    }

    // Iterate over the range (inclusive on both ends)
    for (int offset_x = base_x; step_x > 0 ? offset_x <= end_x : offset_x >= end_x; offset_x += step_x) {
      for (int offset_y = base_y; step_y > 0 ? offset_y <= end_y : offset_y >= end_y; offset_y += step_y) {
        for (int offset_z = base_z; step_z > 0 ? offset_z <= end_z : offset_z >= end_z; offset_z += step_z) {
          const int world_x = origin_x + offset_x;
          const int world_y = origin_y + offset_y;
          const int world_z = origin_z + offset_z;

          // Combined bounds check using unsigned arithmetic
          const unsigned local_x = world_x - state->base_x;
          const unsigned local_y = world_y - state->base_y;
          const unsigned local_z = world_z - state->base_z;

          if (local_x < 16u && local_y < 16u && local_z < 16u) {
            // Optimized block index calculation
            const int block_index = (local_y << 8) | (local_z << 4) | local_x;
            state->block_indices[block_index] = palette_index;
          }
        }
      }
    }
    break;
  }

  case INSTR_FOR_LOOP: {
    ForLoop *loop = &instr->for_loop;

    int start = (int)painter_evaluate_expression(loop->start, state);
    int end = (int)painter_evaluate_expression(loop->end, state);

    for (int i = start; i < end; i++) {
      context_set(state->variables, loop->variable, (double)i);
      run_instruction_list(&loop->body, state, origin_x, origin_y, origin_z);
    }
    break;
  }

  case INSTR_IF_STATEMENT: {
    IfStatement *if_stmt = &instr->if_statement;

    // Evaluate each branch in order
    for (size_t i = 0; i < if_stmt->branch_count; i++) {
      ConditionalBranch *branch = &if_stmt->branches[i];

      // If condition is NULL, this is an 'else' branch (always execute)
      bool should_execute = (branch->condition == NULL);

      // Otherwise evaluate the condition
      if (!should_execute) {
        double condition_value = painter_evaluate_expression(branch->condition, state);
        should_execute = (condition_value != 0.0); // non-zero is true
      }

      if (should_execute) {
        run_instruction_list(&branch->body, state, origin_x, origin_y, origin_z);
        break; // Don't check remaining elif/else branches
      }
    }
    break;
  }

  case INSTR_MACRO_CALL: {
    MacroCall *macro_call = &instr->macro_call;
    MacroGenerator generator = macro_registry_lookup(state->macros, macro_call->name);
    if (generator) {
      generator(state, &macro_call->arguments);
    }
    break;
  }

  case INSTR_OCCURRENCE: {
    execute_occurrence_instruction(&instr->occurrence, state, origin_x, origin_y, origin_z);
    break;
  }

  case INSTR_ASSIGNMENT: {
    if (instr->assignment.is_palette_definition) {
      context_set_palette(state->variables, instr->assignment.name, instr->assignment.palette_definition);
    } else {
      Assignment *assignment = &instr->assignment;

      // Check if the value is an array expression
      if (assignment->value && assignment->value->type == EXPR_ARRAY) {
        // Evaluate each array element
        size_t count = assignment->value->array.elements.count;
        if (count > 0) {
          double *values = malloc(sizeof(double) * count);
          if (values) {
            for (size_t i = 0; i < count; i++) {
              values[i] = painter_evaluate_expression(assignment->value->array.elements.items[i], state);
            }
            context_set_array(state->variables, assignment->name, values, count);
            free(values);
          }
        } else {
          // Empty array
          context_set_array(state->variables, assignment->name, NULL, 0);
        }
      } else {
        // Regular numeric value
        double value = painter_evaluate_expression(assignment->value, state);
        context_set(state->variables, assignment->name, value);
      }
    }
    break;
  }
  }
}

static void
process_program_at_origin(Program *program, ExecutionState *state, int origin_x, int origin_y, int origin_z, bool occurrences_only) {
  if (!program || !state) return;

  for (int i = 0; i < program->instruction_count; i++) {
    Instruction *instr = program->instructions[i];
    if (occurrences_only && instr->type != INSTR_OCCURRENCE) continue;
    if (!instruction_might_affect_section(instr, state, origin_x, origin_y, origin_z)) continue;
    process_instruction(instr, state, origin_x, origin_y, origin_z);
  }
}

// Global shared registries (initialized once, reused across all sections)
// This is safe because builtin functions/macros/occurrences are read-only after initialization
static struct {
  MacroRegistry macros;
  FunctionRegistry functions;
  OccurrenceTypeRegistry occurrence_types;
  bool initialized;
} g_builtin_registries = {0};

// Initialize global registries on first use (thread-safe for read-only access)
static void ensure_builtin_registries_initialized(void) {
  if (g_builtin_registries.initialized) {
    return;
  }

  macro_registry_init(&g_builtin_registries.macros);
  register_builtin_macros(&g_builtin_registries.macros);

  function_registry_init(&g_builtin_registries.functions);
  register_builtin_functions(&g_builtin_registries.functions);

  occurrence_type_registry_init(&g_builtin_registries.occurrence_types);
  register_builtin_occurrences(&g_builtin_registries.occurrence_types);

  g_builtin_registries.initialized = true;
}

// Helper to initialize execution state with all registries
static inline void init_execution_state(ExecutionState *state, Section *section, int *block_indices, int base_x, int base_y, int base_z,
    int *palette_capacity, VariableContext *ctx, OccurrenceRegistry *occurrence_registry) {

  // Ensure global registries are initialized
  ensure_builtin_registries_initialized();

  // Initialize per-section context and occurrence registry
  context_init(ctx);
  occurrence_registry_init(occurrence_registry);

  // Setup execution state (using global shared registries)
  *state = (ExecutionState){
      .variables = ctx,
      .macros = &g_builtin_registries.macros,
      .functions = &g_builtin_registries.functions,
      .occurrences = occurrence_registry,
      .occurrence_types = &g_builtin_registries.occurrence_types,
      .base_x = base_x,
      .base_y = base_y,
      .base_z = base_z,
      .current_origin_x = 0,
      .current_origin_y = 0,
      .current_origin_z = 0,
      .block_indices = block_indices,
      .palette = &section->palette,
      .palette_size = &section->palette_size,
      .palette_capacity = palette_capacity,
  };
}

// Helper to cleanup execution state
static inline void cleanup_execution_state(ExecutionState *state, OccurrenceRegistry *occurrence_registry) {
  occurrence_registry_free(occurrence_registry);
  context_free(state->variables);
  // Note: Global registries are NOT freed - they persist for reuse
}

// Optimized neighbor checking - only check faces and edges that could affect section
static const struct {
  int dx, dy, dz;
} neighbor_offsets[] = {
    // Face neighbors (most likely to affect section)
    {-1, 0, 0},
    {1, 0, 0}, // X faces
    {0, -1, 0},
    {0, 1, 0}, // Y faces
    {0, 0, -1},
    {0, 0, 1}, // Z faces
    // Edge neighbors
    {-1, -1, 0},
    {-1, 1, 0},
    {1, -1, 0},
    {1, 1, 0},
    {-1, 0, -1},
    {-1, 0, 1},
    {1, 0, -1},
    {1, 0, 1},
    {0, -1, -1},
    {0, -1, 1},
    {0, 1, -1},
    {0, 1, 1},
    // Corner neighbors
    {-1, -1, -1},
    {-1, -1, 1},
    {-1, 1, -1},
    {-1, 1, 1},
    {1, -1, -1},
    {1, -1, 1},
    {1, 1, -1},
    {1, 1, 1},
};

// Section generation function
Section *generate_section(Program *program, int section_x, int section_y, int section_z) {
  if (!program) return NULL;

  Section *section = calloc(1, sizeof(*section));
  if (!section) return NULL;

  int block_indices[4096] = {0};
  const int base_x = section_x * 16;
  const int base_y = section_y * 16;
  const int base_z = section_z * 16;
  int palette_capacity = 0;

  // Only need per-section variable context and occurrence registry
  VariableContext ctx;
  OccurrenceRegistry occurrence_registry;

  ExecutionState state;
  init_execution_state(&state, section, block_indices, base_x, base_y, base_z, &palette_capacity, &ctx, &occurrence_registry);

  // Initialize air block
  if (painter_palette_get_or_add(&state, "air") < 0) {
    cleanup_execution_state(&state, &occurrence_registry);
    free(section);
    return NULL;
  }

  // Process all instructions from origin [0, 0, 0]
  process_program_at_origin(program, &state, 0, 0, 0, false);

  // Check for occurrences from neighboring sections (optimized loop)
  // This enables cross-section structures like trees
  for (size_t i = 0; i < sizeof(neighbor_offsets) / sizeof(neighbor_offsets[0]); i++) {
    const int neighbor_origin_x = neighbor_offsets[i].dx * 16;
    const int neighbor_origin_y = neighbor_offsets[i].dy * 16;
    const int neighbor_origin_z = neighbor_offsets[i].dz * 16;
    process_program_at_origin(program, &state, neighbor_origin_x, neighbor_origin_y, neighbor_origin_z, true);
  }

  // Pack block data
  section->bits_per_entry = calculate_bits_per_entry(section->palette_size);

  if (section->bits_per_entry == 0) {
    section->data_size = 0;
    section->data = NULL;
  } else {
    const int bits_per_entry = section->bits_per_entry;
    int blocks_per_long = bits_per_entry >= 64 ? 1 : 64 / bits_per_entry;
    if (blocks_per_long <= 0) {
      blocks_per_long = 1;
    }

    section->data_size = (4096 + blocks_per_long - 1) / blocks_per_long;
    section->data = section->data_size > 0 ? calloc((size_t)section->data_size, sizeof(uint64_t)) : NULL;

    if (!section->data) {
      cleanup_execution_state(&state, &occurrence_registry);
      section_free(section);
      return NULL;
    }

    // Pack palette indices while keeping each value within a single 64-bit word.
    int long_index = 0;
    int offset_in_long = 0;
    int blocks_in_current_long = 0;

    for (int i = 0; i < 4096; i++) {
      if (blocks_in_current_long >= blocks_per_long) {
        long_index++;
        blocks_in_current_long = 0;
        offset_in_long = 0;
      }

      section->data[long_index] |= ((uint64_t)block_indices[i]) << offset_in_long;

      blocks_in_current_long++;
      offset_in_long += bits_per_entry;
      if (offset_in_long >= 64) {
        offset_in_long = 0;
      }
    }
  }

  cleanup_execution_state(&state, &occurrence_registry);
  return section;
}

// Section cleanup
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

// Palette helpers shared between the interpreter and macros

// Helper function to find or add a block to the palette (optimized with linear search)
int painter_palette_get_or_add(ExecutionState *state, const char *block_string) {
  if (!state || !state->palette || !state->palette_size || !state->palette_capacity || !block_string) {
    return -1;
  }

  char **palette = *state->palette;
  const int size = *state->palette_size;

  // Linear search with early exit (typical palettes are small < 50 entries)
  for (int i = 0; i < size; i++) {
    if (strcmp(palette[i], block_string) == 0) {
      return i;
    }
  }

  // Need to add new entry
  int capacity = *state->palette_capacity;
  if (capacity <= size) {
    const int new_capacity = capacity == 0 ? 8 : capacity * 2;
    char **resized = realloc(palette, sizeof(char *) * new_capacity);
    if (!resized) {
      return -1;
    }
    palette = resized;
    *state->palette = palette;
    *state->palette_capacity = new_capacity;
  }

  const size_t len = strlen(block_string);
  char *copy = malloc(len + 1);
  if (!copy) {
    return -1;
  }
  memcpy(copy, block_string, len + 1);

  palette[size] = copy;
  *state->palette_size = size + 1;
  return size;
}

// Helper function to create a full block string (name + properties)
void painter_format_block(char *buffer, size_t buffer_size, const char *block_name, const char *block_properties) {
  if (!buffer || buffer_size == 0) return;
  if (block_properties && block_properties[0] != '\0') {
    snprintf(buffer, buffer_size, "%s[%s]", block_name, block_properties);
  } else {
    snprintf(buffer, buffer_size, "%s", block_name);
  }
}

bool painter_section_contains_point(const ExecutionState *state, int x, int y, int z) {
  if (!state) return false;

  const int max_x = state->base_x + 15;
  const int max_y = state->base_y + 15;
  const int max_z = state->base_z + 15;

  return x >= state->base_x && x <= max_x && y >= state->base_y && y <= max_y && z >= state->base_z && z <= max_z;
}

bool painter_section_clip_aabb(const ExecutionState *state, PainterAABB *box) {
  if (!state || !box) return false;

  const int section_min_x = state->base_x;
  const int section_min_y = state->base_y;
  const int section_min_z = state->base_z;
  const int section_max_x = section_min_x + 15;
  const int section_max_y = section_min_y + 15;
  const int section_max_z = section_min_z + 15;

  if (box->max_x < section_min_x || box->min_x > section_max_x || box->max_y < section_min_y || box->min_y > section_max_y ||
      box->max_z < section_min_z || box->min_z > section_max_z) {
    return false;
  }

  if (box->min_x < section_min_x) box->min_x = section_min_x;
  if (box->max_x > section_max_x) box->max_x = section_max_x;
  if (box->min_y < section_min_y) box->min_y = section_min_y;
  if (box->max_y > section_max_y) box->max_y = section_max_y;
  if (box->min_z < section_min_z) box->min_z = section_min_z;
  if (box->max_z > section_max_z) box->max_z = section_max_z;

  return true;
}

// Expression evaluation helpers
int painter_eval_offset(const Expression *expr, ExecutionState *state) { return expr ? (int)painter_evaluate_expression(expr, state) : 0; }

bool painter_eval_positive_extent(const Expression *expr, ExecutionState *state, int *out_extent) {
  if (!expr || !out_extent) {
    return false;
  }

  const double value = painter_evaluate_expression(expr, state);
  if (!isfinite(value) || value <= 0.0) {
    return false;
  }

  const long long rounded = llround(value);
  if (rounded <= 0) {
    return false;
  }

  *out_extent = (int)rounded;
  return true;
}

bool painter_eval_boolean_flag(const Expression *expr, ExecutionState *state) {
  if (!expr) {
    return false;
  }
  return painter_evaluate_expression(expr, state) != 0.0;
}

bool painter_eval_coordinate_argument(const Expression *expr, ExecutionState *state, int *out_x, int *out_y, int *out_z) {
  if (!expr || !state || !out_x || !out_y || !out_z) {
    return false;
  }

  // Handle EXPR_ARRAY as coordinate (arrays of 2 or 3 elements)
  if (expr->type == EXPR_ARRAY) {
    size_t count = expr->array.elements.count;
    if (count < 2 || count > 3) {
      return false; // Arrays must have 2 or 3 elements to be used as coordinates
    }

    const double x_val = painter_evaluate_expression(expr->array.elements.items[0], state);
    const double y_val = count == 3 ? painter_evaluate_expression(expr->array.elements.items[1], state) : 0.0;
    const double z_val = count == 3 ? painter_evaluate_expression(expr->array.elements.items[2], state)
                                    : painter_evaluate_expression(expr->array.elements.items[1], state);

    *out_x = state->current_origin_x + (int)llround(x_val);
    *out_y = state->current_origin_y + (int)llround(y_val);
    *out_z = state->current_origin_z + (int)llround(z_val);
    return true;
  }

  return false;
}

bool painter_eval_positive_component_or_default(const Expression *expr, ExecutionState *state, int default_value, int *out_value) {
  if (!out_value) {
    return false;
  }
  if (!expr) {
    if (default_value <= 0) {
      return false;
    }
    *out_value = default_value;
    return true;
  }
  const double value = painter_evaluate_expression(expr, state);
  if (!isfinite(value)) {
    return false;
  }
  const long long rounded = llround(value);
  if (rounded <= 0) {
    return false;
  }
  *out_value = (int)rounded;
  return true;
}

bool painter_eval_positive_vector3(
    const Expression *expr, ExecutionState *state, int default_y, int default_z, int *out_x, int *out_y, int *out_z) {
  if (!expr || !out_x || !out_y || !out_z) {
    return false;
  }

  const Expression *x_expr = expr;
  const Expression *y_expr = NULL;
  const Expression *z_expr = NULL;

  if (expr->type == EXPR_ARRAY) {
    // Handle array as coordinate
    size_t count = expr->array.elements.count;
    if (count >= 1) {
      x_expr = expr->array.elements.items[0];
    }
    if (count >= 2) {
      if (count == 2) {
        // 2 elements: [x, z], y defaults
        z_expr = expr->array.elements.items[1];
      } else {
        // 3+ elements: [x, y, z]
        y_expr = expr->array.elements.items[1];
        z_expr = expr->array.elements.items[2];
      }
    }
  }

  if (!x_expr) {
    return false;
  }

  if (!painter_eval_positive_component_or_default(x_expr, state, -1, out_x)) {
    return false;
  }
  if (!painter_eval_positive_component_or_default(y_expr, state, default_y, out_y)) {
    return false;
  }
  if (!painter_eval_positive_component_or_default(z_expr, state, default_z, out_z)) {
    return false;
  }
  return true;
}

double painter_eval_double_or(const Expression *expr, ExecutionState *state, double fallback) {
  return expr ? painter_evaluate_expression(expr, state) : fallback;
}

int painter_eval_int_or(const Expression *expr, ExecutionState *state, int fallback) {
  return (int)llround(painter_eval_double_or(expr, state, (double)fallback));
}
