#include "builtin_macros.h"
#include "painter_eval.h"

static inline int world_to_local(int world_coord, int section_base) { return world_coord - section_base; }

static int add_block_to_palette(ExecutionState *state, const Expression *block_expr) {
  if (!state || !block_expr || block_expr->type != EXPR_IDENTIFIER) {
    return -1;
  }

  char block_string[MAX_TOKEN_VALUE_LENGTH];
  painter_format_block(block_string, sizeof(block_string), block_expr->identifier, "");
  return painter_palette_get_or_add(state, block_string);
}

static void write_block_if_visible(ExecutionState *state, int world_x, int world_y, int world_z, int palette_index) {
  if (!painter_section_contains_point(state, world_x, world_y, world_z) || palette_index < 0) {
    return;
  }

  const int local_x = world_to_local(world_x, state->base_x);
  const int local_y = world_to_local(world_y, state->base_y);
  const int local_z = world_to_local(world_z, state->base_z);
  const int index = local_y * 256 + local_z * 16 + local_x;
  state->block_indices[index] = palette_index;
}

static int eval_offset(const Expression *expr, ExecutionState *state) { return expr ? (int)painter_evaluate_expression(expr, state) : 0; }

// Sphere macro: #sphere .x=<x> .y=<y> .z=<z> .radius=<radius> .block=<block_name>
void builtin_macro_sphere(ExecutionState *state, const NamedArgumentList *args) {
  if (!state) return;

  Expression *radius_expr = named_arg_get(args, "radius");
  Expression *block_expr = named_arg_get(args, "block");
  if (!radius_expr || !block_expr) return;

  const int radius = (int)painter_evaluate_expression(radius_expr, state);
  if (radius < 0) return;

  const int palette_index = add_block_to_palette(state, block_expr);
  if (palette_index < 0) return;

  const int center_x = state->current_origin_x + eval_offset(named_arg_get(args, "x"), state);
  const int center_y = state->current_origin_y + eval_offset(named_arg_get(args, "y"), state);
  const int center_z = state->current_origin_z + eval_offset(named_arg_get(args, "z"), state);

  if (radius == 0) {
    write_block_if_visible(state, center_x, center_y, center_z, palette_index);
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
    return;
  }

  const int radius_sq = radius * radius;
  const int section_min_x = state->base_x;
  const int section_min_y = state->base_y;
  const int section_min_z = state->base_z;

  for (int world_y = bounds.min_y; world_y <= bounds.max_y; ++world_y) {
    const int dy = world_y - center_y;
    const int dy_sq = dy * dy;
    const int local_y = world_to_local(world_y, section_min_y);
    const int y_offset = local_y * 256;

    for (int world_z = bounds.min_z; world_z <= bounds.max_z; ++world_z) {
      const int dz = world_z - center_z;
      const int dz_sq = dz * dz;
      const int local_z = world_to_local(world_z, section_min_z);
      const int base_index = y_offset + local_z * 16;

      for (int world_x = bounds.min_x; world_x <= bounds.max_x; ++world_x) {
        const int dx = world_x - center_x;
        if (dx * dx + dy_sq + dz_sq > radius_sq) continue;

        const int local_x = world_to_local(world_x, section_min_x);
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
