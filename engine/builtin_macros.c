#include "builtin_macros.h"
#include "painter_eval.h"

// Sphere macro: #sphere .x=<x> .y=<y> .z=<z> .radius=<radius> .block=<block_name>
// Generates a filled 3D sphere
void builtin_macro_sphere(ExecutionState *state, const NamedArgumentList *args) {
  if (!state) {
    return;
  }

  // Get arguments
  Expression *x_expr = named_arg_get(args, "x");
  Expression *y_expr = named_arg_get(args, "y");
  Expression *z_expr = named_arg_get(args, "z");
  Expression *radius_expr = named_arg_get(args, "radius");
  Expression *block_expr = named_arg_get(args, "block");

  if (!radius_expr || !block_expr) {
    return; // Missing required arguments
  }

  if (block_expr->type != EXPR_IDENTIFIER) {
    return; // Block must be an identifier
  }

  const int offset_x = x_expr ? (int)painter_evaluate_expression(x_expr, state) : 0;
  const int offset_y = y_expr ? (int)painter_evaluate_expression(y_expr, state) : 0;
  const int offset_z = z_expr ? (int)painter_evaluate_expression(z_expr, state) : 0;
  const int center_x = state->current_origin_x + offset_x;
  const int center_y = state->current_origin_y + offset_y;
  const int center_z = state->current_origin_z + offset_z;
  const int radius = (int)painter_evaluate_expression(radius_expr, state);

  if (radius < 0) {
    return; // Negative radius is treated as no-op for consistency with previous behaviour
  }

  if (radius == 0) {
    // Single block placement shortcut once the palette index is known.
    char block_string[MAX_TOKEN_VALUE_LENGTH];
    painter_format_block(block_string, sizeof(block_string), block_expr->identifier, "");

    if (!painter_section_contains_point(state, center_x, center_y, center_z)) {
      return;
    }

    const int palette_index = painter_palette_get_or_add(state, block_string);
    if (palette_index < 0) {
      return;
    }

    const int local_x = center_x - state->base_x;
    const int local_y = center_y - state->base_y;
    const int local_z = center_z - state->base_z;
    const int index = local_y * 256 + local_z * 16 + local_x;
    state->block_indices[index] = palette_index;
    return;
  }

  PainterAABB bounds = {
      .min_x = center_x - radius,
      .max_x = center_x + radius,
      .min_y = center_y - radius,
      .max_y = center_y + radius,
      .min_z = center_z - radius,
      .max_z = center_z + radius,
  };

  if (!painter_section_clip_aabb(state, &bounds)) {
    return; // Sphere does not intersect this section
  }

  char block_string[MAX_TOKEN_VALUE_LENGTH];
  painter_format_block(block_string, sizeof(block_string), block_expr->identifier, "");

  const int palette_index = painter_palette_get_or_add(state, block_string);
  if (palette_index < 0) {
    return;
  }

  const int radius_sq = radius * radius;
  const int section_min_x = state->base_x;
  const int section_min_y = state->base_y;
  const int section_min_z = state->base_z;

  for (int world_y = bounds.min_y; world_y <= bounds.max_y; ++world_y) {
    const int dy = world_y - center_y;
    const int dy_sq = dy * dy;
    const int local_y = world_y - section_min_y;
    const int y_offset = local_y * 256;

    for (int world_z = bounds.min_z; world_z <= bounds.max_z; ++world_z) {
      const int dz = world_z - center_z;
      const int dz_sq = dz * dz;
      const int local_z = world_z - section_min_z;
      const int base_index = y_offset + local_z * 16;

      for (int world_x = bounds.min_x; world_x <= bounds.max_x; ++world_x) {
        const int dx = world_x - center_x;
        if (dx * dx + dy_sq + dz_sq > radius_sq) {
          continue;
        }

        const int local_x = world_x - section_min_x;
        state->block_indices[base_index + local_x] = palette_index;
      }
    }
  }
}

const BuiltinMacro BUILTIN_MACROS[] = {
    {"sphere", builtin_macro_sphere},
};

const int BUILTIN_MACROS_COUNT = sizeof(BUILTIN_MACROS) / sizeof(BuiltinMacro);

void register_builtin_macros(MacroRegistry *registry) {
  for (int i = 0; i < BUILTIN_MACROS_COUNT; i++) {
    macro_registry_register(registry, BUILTIN_MACROS[i].name, BUILTIN_MACROS[i].generator);
  }
}
