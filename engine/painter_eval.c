#include "painter_eval.h"
#include "builtin_functions.h"
#include "builtin_macros.h"
#include "builtin_occurrences.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for internal functions
static void execute_occurrence_instruction(const Occurrence *occurrence, ExecutionState *state, int origin_x, int origin_y, int origin_z);
static bool instruction_might_affect_section(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z);
static bool evaluate_block_position(const BlockPlacement *placement, ExecutionState *state, int origin_x, int origin_y, int origin_z,
    int *world_x, int *world_y, int *world_z);
static void apply_palette_definition(ExecutionState *state, const PaletteDefinition *definition);
static void clear_runtime_palette(ExecutionState *state);
static bool ensure_runtime_palette_capacity(ExecutionState *state, size_t required_capacity);
static int calculate_bits_per_entry(int palette_size);
static void execute_occurrence_by_type(const char *type, const NamedArgumentList *args, const InstructionList *body, ExecutionState *state,
    int origin_x, int origin_y, int origin_z);
static void occurrence_runtime_run_body(void *userdata, const InstructionList *body, int anchor_x, int anchor_y, int anchor_z);

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

// Ensure palette capacity
static bool ensure_runtime_palette_capacity(ExecutionState *state, size_t required_capacity) {
  if (!state || !state->palette || !state->palette_capacity) {
    return false;
  }

  int required = (int)required_capacity;
  if (*state->palette_capacity >= required) {
    return true;
  }

  int new_capacity = *state->palette_capacity;
  if (new_capacity <= 0) {
    new_capacity = 8;
  }

  while (new_capacity < required) {
    new_capacity *= 2;
  }

  char **resized = realloc(*state->palette, sizeof(char *) * new_capacity);
  if (!resized) {
    return false;
  }

  *state->palette = resized;
  *state->palette_capacity = new_capacity;
  return true;
}

// Clear runtime palette
static void clear_runtime_palette(ExecutionState *state) {
  if (!state || !state->palette || !state->palette_size) {
    return;
  }

  for (int i = 0; i < *state->palette_size; i++) {
    free((*state->palette)[i]);
    (*state->palette)[i] = NULL;
  }
  *state->palette_size = 0;
}

// Apply palette definition
static void apply_palette_definition(ExecutionState *state, const PaletteDefinition *definition) {
  if (!state || !definition) {
    return;
  }

  clear_runtime_palette(state);

  if (!ensure_runtime_palette_capacity(state, definition->entries.count)) {
    return;
  }

  for (size_t i = 0; i < definition->entries.count; i++) {
    const PaletteEntry *entry = &definition->entries.items[i];
    char block_string[MAX_TOKEN_VALUE_LENGTH * 2];
    painter_format_block(block_string, sizeof(block_string), entry->block_name, entry->block_properties);

    size_t length = strlen(block_string);
    char *copy = malloc(length + 1);
    if (!copy) {
      return;
    }
    memcpy(copy, block_string, length + 1);

    (*state->palette)[i] = copy;
    *state->palette_size = (int)(i + 1);
  }
}

// Evaluate block position
static bool evaluate_block_position(const BlockPlacement *placement, ExecutionState *state, int origin_x, int origin_y, int origin_z,
    int *world_x, int *world_y, int *world_z) {
  if (!placement || !state) return false;

  if (!placement->coordinate || placement->coordinate->type != EXPR_COORDINATE) {
    return false;
  }

  int offset_x = (int)painter_evaluate_expression(placement->coordinate->coordinate.x, state);
  int offset_y = placement->coordinate->coordinate.y ? (int)painter_evaluate_expression(placement->coordinate->coordinate.y, state) : 0;
  int offset_z = placement->coordinate->coordinate.z ? (int)painter_evaluate_expression(placement->coordinate->coordinate.z, state) : 0;

  if (world_x) *world_x = origin_x + offset_x;
  if (world_y) *world_y = origin_y + offset_y;
  if (world_z) *world_z = origin_z + offset_z;
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

    double *arg_values = NULL;
    if (arg_count > 0) {
      arg_values = malloc(sizeof(double) * arg_count);
      if (!arg_values) {
        return 0.0;
      }
      for (size_t i = 0; i < arg_count; i++) {
        arg_values[i] = painter_evaluate_expression(expr->function_call.args.items[i], state);
      }
    }

    double result = entry->function ? entry->function(arg_values, arg_count) : 0.0;
    free(arg_values);
    return result;
  }
  case EXPR_COORDINATE: break;
  }

  return 0.0;
}

// Check if instruction might affect section
static bool instruction_might_affect_section(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z) {
  if (!instr || !state) return false;

  switch (instr->type) {
  case INSTR_BLOCK_PLACEMENT: {
    int world_x = 0;
    int world_y = 0;
    int world_z = 0;
    if (!evaluate_block_position(&instr->block_placement, state, origin_x, origin_y, origin_z, &world_x, &world_y, &world_z)) {
      return false;
    }
    return painter_section_contains_point(state, world_x, world_y, world_z);
  }
  case INSTR_OCCURRENCE: {
    const Occurrence *occ = &instr->occurrence;
    if (!occ->body.has_bounds) {
      // No bounds info, conservatively assume it might affect this section
      return true;
    }
    
    // Check if the occurrence's bounding box (offset by origin) intersects current section
    int section_x = state->base_x / 16;
    int section_y = state->base_y / 16;
    int section_z = state->base_z / 16;
    
  // Calculate world-space bounds. The origin parameters are offsets relative
  // to the current section base (state->base_*). Include state->base_* so
  // the computed world bounds reflect the true world coordinates for this
  // origin (which may be non-zero when checking neighboring sections).
  int world_min_x = state->base_x + origin_x + occ->body.min_x;
  int world_max_x = state->base_x + origin_x + occ->body.max_x;
  int world_min_y = state->base_y + origin_y + occ->body.min_y;
  int world_max_y = state->base_y + origin_y + occ->body.max_y;
  int world_min_z = state->base_z + origin_z + occ->body.min_z;
  int world_max_z = state->base_z + origin_z + occ->body.max_z;
    
    // Check intersection with current section
    int sec_min_x = state->base_x;
    int sec_max_x = state->base_x + 15;
    int sec_min_y = state->base_y;
    int sec_max_y = state->base_y + 15;
    int sec_min_z = state->base_z;
    int sec_max_z = state->base_z + 15;
    
    return !(world_max_x < sec_min_x || world_min_x > sec_max_x ||
             world_max_y < sec_min_y || world_min_y > sec_max_y ||
             world_max_z < sec_min_z || world_min_z > sec_max_z);
  }
  default: return true;
  }
}

// Occurrence runtime callback
static void occurrence_runtime_run_body(void *userdata, const InstructionList *body, int anchor_x, int anchor_y, int anchor_z) {
  ExecutionState *state = (ExecutionState *)userdata;
  if (!state || !body) {
    return;
  }

  for (size_t i = 0; i < body->count; i++) {
    Instruction *child = body->items[i];
    if (!instruction_might_affect_section(child, state, anchor_x, anchor_y, anchor_z)) {
      continue;
    }
    process_instruction(child, state, anchor_x, anchor_y, anchor_z);
  }
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
    int world_x = 0;
    int world_y = 0;
    int world_z = 0;
    if (!evaluate_block_position(&instr->block_placement, state, origin_x, origin_y, origin_z, &world_x, &world_y, &world_z)) {
      break;
    }

    if (!painter_section_contains_point(state, world_x, world_y, world_z)) {
      break;
    }

    int local_x = world_x - state->base_x;
    int local_y = world_y - state->base_y;
    int local_z = world_z - state->base_z;
    int block_index = local_y * 256 + local_z * 16 + local_x;

    char block_string[MAX_TOKEN_VALUE_LENGTH * 2];
    painter_format_block(block_string, sizeof(block_string), instr->block_placement.block_name, instr->block_placement.block_properties);

    int palette_index = painter_palette_get_or_add(state, block_string);
    if (palette_index >= 0) {
      state->block_indices[block_index] = palette_index;
    }
    break;
  }

  case INSTR_ASSIGNMENT: {
    Assignment *assignment = &instr->assignment;
    double value = painter_evaluate_expression(assignment->value, state);
    context_set(state->variables, assignment->name, value);
    break;
  }

  case INSTR_FOR_LOOP: {
    ForLoop *loop = &instr->for_loop;

    int start = (int)painter_evaluate_expression(loop->start, state);
    int end = (int)painter_evaluate_expression(loop->end, state);

    for (int i = start; i < end; i++) {
      context_set(state->variables, loop->variable, (double)i);

      for (size_t j = 0; j < loop->body.count; j++) {
        Instruction *child = loop->body.items[j];
        if (!instruction_might_affect_section(child, state, origin_x, origin_y, origin_z)) {
          continue;
        }
        process_instruction(child, state, origin_x, origin_y, origin_z);
      }
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
        // Execute this branch and stop checking further branches
        for (size_t j = 0; j < branch->body.count; j++) {
          Instruction *child = branch->body.items[j];
          if (!instruction_might_affect_section(child, state, origin_x, origin_y, origin_z)) {
            continue;
          }
          process_instruction(child, state, origin_x, origin_y, origin_z);
        }
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

  case INSTR_PALETTE_DEFINITION: apply_palette_definition(state, &instr->palette_definition); break;
  }
}

// Section generation function
Section *generate_section(Program *program, int section_x, int section_y, int section_z) {
  if (!program) return NULL;

  Section *section = calloc(1, sizeof(*section));
  if (!section) return NULL;

  int block_indices[4096] = {0};

  const int base_x = section_x * 16;
  const int base_y = section_y * 16;
  const int base_z = section_z * 16;

  VariableContext ctx;
  context_init(&ctx);

  MacroRegistry macro_registry;
  macro_registry_init(&macro_registry);
  register_builtin_macros(&macro_registry);

  FunctionRegistry function_registry;
  function_registry_init(&function_registry);
  register_builtin_functions(&function_registry);

  OccurrenceTypeRegistry occurrence_type_registry;
  occurrence_type_registry_init(&occurrence_type_registry);
  register_builtin_occurrences(&occurrence_type_registry);

  OccurrenceRegistry occurrence_registry;
  occurrence_registry_init(&occurrence_registry);

  int palette_capacity = 0;
  bool success = false;

  ExecutionState state = {
      .variables = &ctx,
      .macros = &macro_registry,
      .functions = &function_registry,
      .occurrences = &occurrence_registry,
      .occurrence_types = &occurrence_type_registry,
      .base_x = base_x,
      .base_y = base_y,
      .base_z = base_z,
      .current_origin_x = 0,
      .current_origin_y = 0,
      .current_origin_z = 0,
      .block_indices = block_indices,
      .palette = &section->palette,
      .palette_size = &section->palette_size,
      .palette_capacity = &palette_capacity,
  };

  if (painter_palette_get_or_add(&state, "air") < 0) {
    goto cleanup;
  }

  // Process all instructions from origin [0, 0, 0]
  for (int i = 0; i < program->instruction_count; i++) {
    Instruction *instr = program->instructions[i];
    if (!instruction_might_affect_section(instr, &state, 0, 0, 0)) {
      continue;
    }
    process_instruction(instr, &state, 0, 0, 0);
  }
  
  // Check for occurrences from neighboring sections that might affect this section
  // This enables cross-section structures like trees
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      for (int dz = -1; dz <= 1; dz++) {
        if (dx == 0 && dy == 0 && dz == 0) continue; // Skip current section (already processed)
        
        // Calculate origin offset for neighboring section
        int neighbor_origin_x = dx * 16;
        int neighbor_origin_y = dy * 16;
        int neighbor_origin_z = dz * 16;
        
        // Check each instruction to see if it might affect current section from neighbor
        for (int i = 0; i < program->instruction_count; i++) {
          Instruction *instr = program->instructions[i];
          
          // Only check occurrences (as they're the ones that span sections)
          if (instr->type == INSTR_OCCURRENCE) {
            if (instruction_might_affect_section(instr, &state, neighbor_origin_x, neighbor_origin_y, neighbor_origin_z)) {
              process_instruction(instr, &state, neighbor_origin_x, neighbor_origin_y, neighbor_origin_z);
            }
          }
        }
      }
    }
  }

  section->bits_per_entry = calculate_bits_per_entry(section->palette_size);

  if (section->bits_per_entry == 0) {
    section->data_size = 0;
    section->data = NULL;
  } else {
    int blocks_per_long = 64 / section->bits_per_entry;
    section->data_size = (4096 + blocks_per_long - 1) / blocks_per_long;
    section->data = calloc(section->data_size, sizeof(uint64_t));
    if (!section->data) {
      goto cleanup;
    }

    for (int i = 0; i < 4096; i++) {
      int long_index = i / blocks_per_long;
      int offset_in_long = (i % blocks_per_long) * section->bits_per_entry;
      section->data[long_index] |= ((uint64_t)block_indices[i]) << offset_in_long;
    }
  }

  success = true;

cleanup:
  occurrence_registry_free(&occurrence_registry);
  occurrence_type_registry_free(&occurrence_type_registry);
  function_registry_free(&function_registry);
  macro_registry_free(&macro_registry);
  context_free(&ctx);

  if (!success) {
    section_free(section);
    section = NULL;
  }

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

// Helper function to find or add a block to the palette
int painter_palette_get_or_add(ExecutionState *state, const char *block_string) {
  if (!state || !state->palette || !state->palette_size || !state->palette_capacity || !block_string) {
    return -1;
  }

  for (int i = 0; i < *state->palette_size; i++) {
    if (strcmp((*state->palette)[i], block_string) == 0) {
      return i;
    }
  }

  if (*state->palette_capacity <= *state->palette_size) {
    int new_capacity = (*state->palette_capacity == 0) ? 8 : (*state->palette_capacity * 2);
    char **resized = realloc(*state->palette, sizeof(char *) * new_capacity);
    if (!resized) {
      return -1;
    }
    *state->palette = resized;
    *state->palette_capacity = new_capacity;
  }

  char *copy = malloc(strlen(block_string) + 1);
  if (!copy) {
    return -1;
  }
  strcpy(copy, block_string);

  (*state->palette)[*state->palette_size] = copy;
  return (*state->palette_size)++;
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

