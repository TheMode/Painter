#include "builtin_macros.h"
#include "painter_eval.h"

#include <math.h>
#include <stdlib.h>

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

static bool eval_positive_extent(const Expression *expr, ExecutionState *state, int *out_extent) {
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

static bool eval_boolean_flag(const Expression *expr, ExecutionState *state) {
  if (!expr) {
    return false;
  }
  return painter_evaluate_expression(expr, state) != 0.0;
}

static bool eval_coordinate_argument(const Expression *expr, ExecutionState *state, int *out_x, int *out_y, int *out_z) {
  if (!expr || !state || !out_x || !out_y || !out_z) {
    return false;
  }

  if (expr->type != EXPR_COORDINATE || !expr->coordinate.x) {
    return false;
  }

  const double x_val = painter_evaluate_expression(expr->coordinate.x, state);
  const double y_val = expr->coordinate.y ? painter_evaluate_expression(expr->coordinate.y, state) : 0.0;
  const double z_val = expr->coordinate.z ? painter_evaluate_expression(expr->coordinate.z, state) : 0.0;

  *out_x = state->current_origin_x + (int)llround(x_val);
  *out_y = state->current_origin_y + (int)llround(y_val);
  *out_z = state->current_origin_z + (int)llround(z_val);
  return true;
}

static void draw_line(ExecutionState *state, int x0, int y0, int z0, int x1, int y1, int z1, int palette_index) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int dz = abs(z1 - z0);

  const int step_x = (x1 > x0) ? 1 : (x1 < x0 ? -1 : 0);
  const int step_y = (y1 > y0) ? 1 : (y1 < y0 ? -1 : 0);
  const int step_z = (z1 > z0) ? 1 : (z1 < z0 ? -1 : 0);

  write_block_if_visible(state, x0, y0, z0, palette_index);

  if (dx >= dy && dx >= dz) {
    int err_y = (dy << 1) - dx;
    int err_z = (dz << 1) - dx;
    while (x0 != x1) {
      x0 += step_x;
      if (err_y >= 0) {
        y0 += step_y;
        err_y -= (dx << 1);
      }
      if (err_z >= 0) {
        z0 += step_z;
        err_z -= (dx << 1);
      }
      err_y += (dy << 1);
      err_z += (dz << 1);
      write_block_if_visible(state, x0, y0, z0, palette_index);
    }
  } else if (dy >= dx && dy >= dz) {
    int err_x = (dx << 1) - dy;
    int err_z = (dz << 1) - dy;
    while (y0 != y1) {
      y0 += step_y;
      if (err_x >= 0) {
        x0 += step_x;
        err_x -= (dy << 1);
      }
      if (err_z >= 0) {
        z0 += step_z;
        err_z -= (dy << 1);
      }
      err_x += (dx << 1);
      err_z += (dz << 1);
      write_block_if_visible(state, x0, y0, z0, palette_index);
    }
  } else {
    int err_x = (dx << 1) - dz;
    int err_y = (dy << 1) - dz;
    while (z0 != z1) {
      z0 += step_z;
      if (err_x >= 0) {
        x0 += step_x;
        err_x -= (dz << 1);
      }
      if (err_y >= 0) {
        y0 += step_y;
        err_y -= (dz << 1);
      }
      err_x += (dx << 1);
      err_y += (dy << 1);
      write_block_if_visible(state, x0, y0, z0, palette_index);
    }
  }
}

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

// Cuboid macro: #cuboid .from=[x, y, z] .to=[x, y, z] .block=<block_name> [.hollow]
void builtin_macro_cuboid(ExecutionState *state, const NamedArgumentList *args) {
  if (!state) return;

  Expression *block_expr = named_arg_get(args, "block");
  if (!block_expr) {
    return;
  }

  const int palette_index = add_block_to_palette(state, block_expr);
  if (palette_index < 0) {
    return;
  }

  const Expression *from_expr = named_arg_get(args, "from");
  const Expression *to_expr = named_arg_get(args, "to");

  int min_x = 0;
  int min_y = 0;
  int min_z = 0;
  int max_x = 0;
  int max_y = 0;
  int max_z = 0;

  bool have_from_to = false;
  if (from_expr && to_expr) {
    int from_x = 0;
    int from_y = 0;
    int from_z = 0;
    int to_x = 0;
    int to_y = 0;
    int to_z = 0;
    if (!eval_coordinate_argument(from_expr, state, &from_x, &from_y, &from_z) ||
        !eval_coordinate_argument(to_expr, state, &to_x, &to_y, &to_z)) {
      return;
    }

    min_x = from_x < to_x ? from_x : to_x;
    max_x = from_x > to_x ? from_x : to_x;
    min_y = from_y < to_y ? from_y : to_y;
    max_y = from_y > to_y ? from_y : to_y;
    min_z = from_z < to_z ? from_z : to_z;
    max_z = from_z > to_z ? from_z : to_z;
    have_from_to = true;
  }

  if (!have_from_to) {
    int width = 0;
    int height = 0;
    int depth = 0;

    if (!eval_positive_extent(named_arg_get(args, "width"), state, &width) ||
        !eval_positive_extent(named_arg_get(args, "height"), state, &height) ||
        !eval_positive_extent(named_arg_get(args, "depth"), state, &depth)) {
      return;
    }

    const int origin_x = state->current_origin_x + eval_offset(named_arg_get(args, "x"), state);
    const int origin_y = state->current_origin_y + eval_offset(named_arg_get(args, "y"), state);
    const int origin_z = state->current_origin_z + eval_offset(named_arg_get(args, "z"), state);

    min_x = origin_x;
    min_y = origin_y;
    min_z = origin_z;
    max_x = origin_x + width - 1;
    max_y = origin_y + height - 1;
    max_z = origin_z + depth - 1;
  }

  PainterAABB bounds = {
      .min_x = min_x,
      .max_x = max_x,
      .min_y = min_y,
      .max_y = max_y,
      .min_z = min_z,
      .max_z = max_z,
  };

  if (!painter_section_clip_aabb(state, &bounds)) {
    return;
  }

  const bool hollow = eval_boolean_flag(named_arg_get(args, "hollow"), state);

  const int section_min_x = state->base_x;
  const int section_min_y = state->base_y;
  const int section_min_z = state->base_z;

  for (int world_y = bounds.min_y; world_y <= bounds.max_y; ++world_y) {
    const int local_y = world_to_local(world_y, section_min_y);
    const int y_offset = local_y * 256;
    const bool y_is_surface = (world_y == min_y) || (world_y == max_y);

    for (int world_z = bounds.min_z; world_z <= bounds.max_z; ++world_z) {
      const int local_z = world_to_local(world_z, section_min_z);
      const int base_index = y_offset + local_z * 16;
      const bool z_is_surface = (world_z == min_z) || (world_z == max_z);

      for (int world_x = bounds.min_x; world_x <= bounds.max_x; ++world_x) {
        if (hollow) {
          const bool x_is_surface = (world_x == min_x) || (world_x == max_x);
          if (!(y_is_surface || z_is_surface || x_is_surface)) {
            continue;
          }
        }

        const int local_x = world_to_local(world_x, section_min_x);
        state->block_indices[base_index + local_x] = palette_index;
      }
    }
  }
}

// Line macro: #line .from=[x, y, z] .to=[x, y, z] .block=<block_name>
void builtin_macro_line(ExecutionState *state, const NamedArgumentList *args) {
  if (!state) return;

  Expression *block_expr = named_arg_get(args, "block");
  const Expression *from_expr = named_arg_get(args, "from");
  const Expression *to_expr = named_arg_get(args, "to");
  if (!block_expr || !from_expr || !to_expr) {
    return;
  }

  int start_x = 0;
  int start_y = 0;
  int start_z = 0;
  if (!eval_coordinate_argument(from_expr, state, &start_x, &start_y, &start_z)) {
    return;
  }

  int end_x = 0;
  int end_y = 0;
  int end_z = 0;
  if (!eval_coordinate_argument(to_expr, state, &end_x, &end_y, &end_z)) {
    return;
  }

  const int palette_index = add_block_to_palette(state, block_expr);
  if (palette_index < 0) {
    return;
  }

  if (start_x == end_x && start_y == end_y && start_z == end_z) {
    write_block_if_visible(state, start_x, start_y, start_z, palette_index);
    return;
  }

  PainterAABB bounds = {
      .min_x = start_x < end_x ? start_x : end_x,
      .max_x = start_x > end_x ? start_x : end_x,
      .min_y = start_y < end_y ? start_y : end_y,
      .max_y = start_y > end_y ? start_y : end_y,
      .min_z = start_z < end_z ? start_z : end_z,
      .max_z = start_z > end_z ? start_z : end_z,
  };

  if (!painter_section_clip_aabb(state, &bounds)) {
    return;
  }

  draw_line(state, start_x, start_y, start_z, end_x, end_y, end_z, palette_index);
}

// Column macro: #column .block=<block_name> (.to=<absolute_y> | .height=<count>) [.x=<dx>] [.y=<dy>] [.z=<dz>]
// .height specifies the NUMBER of blocks to place, not the delta
// For example: .height=2 places 2 blocks (at y and y+1), not 3 blocks (y, y+1, y+2)
void builtin_macro_column(ExecutionState *state, const NamedArgumentList *args) {
  if (!state) return;

  Expression *block_expr = named_arg_get(args, "block");
  if (!block_expr) {
    return;
  }

  const int palette_index = add_block_to_palette(state, block_expr);
  if (palette_index < 0) {
    return;
  }

  const int start_x = state->current_origin_x + eval_offset(named_arg_get(args, "x"), state);
  const int start_y = state->current_origin_y + eval_offset(named_arg_get(args, "y"), state);
  const int start_z = state->current_origin_z + eval_offset(named_arg_get(args, "z"), state);

  const Expression *to_expr = named_arg_get(args, "to");
  const Expression *height_expr = named_arg_get(args, "height");

  if (!to_expr && !height_expr) {
    return;
  }

  int target_y = start_y;
  if (to_expr) {
    const double target_value = painter_evaluate_expression(to_expr, state);
    if (!isfinite(target_value)) {
      return;
    }
    target_y = (int)llround(target_value);
  } else {
    // .height specifies the NUMBER of blocks, so target is start + height - 1
    const double height_value = painter_evaluate_expression(height_expr, state);
    if (!isfinite(height_value)) {
      return;
    }
    const int height_blocks = (int)llround(height_value);
    if (height_blocks == 0) {
      return; // No blocks to place
    }
    // For positive height, target is start + height - 1
    // For negative height, target is start + height + 1
    target_y = start_y + height_blocks + (height_blocks > 0 ? -1 : 1);
  }

  PainterAABB bounds = {
      .min_x = start_x,
      .max_x = start_x,
      .min_y = start_y < target_y ? start_y : target_y,
      .max_y = start_y > target_y ? start_y : target_y,
      .min_z = start_z,
      .max_z = start_z,
  };

  if (!painter_section_clip_aabb(state, &bounds)) {
    return;
  }

  const int step = (target_y >= start_y) ? 1 : -1;
  int first_y;
  int last_y;
  if (step > 0) {
    first_y = start_y < bounds.min_y ? bounds.min_y : start_y;
    last_y = target_y > bounds.max_y ? bounds.max_y : target_y;
    if (first_y > last_y) {
      return;
    }
  } else {
    first_y = start_y > bounds.max_y ? bounds.max_y : start_y;
    last_y = target_y < bounds.min_y ? bounds.min_y : target_y;
    if (first_y < last_y) {
      return;
    }
  }

  const int local_x = world_to_local(start_x, state->base_x);
  const int local_z = world_to_local(start_z, state->base_z);

  int local_y = world_to_local(first_y, state->base_y);
  int index = local_y * 256 + local_z * 16 + local_x;
  const int stride = step * 256;
  const int iterations = (step > 0) ? (last_y - first_y + 1) : (first_y - last_y + 1);

  for (int i = 0; i < iterations; ++i) {
    state->block_indices[index] = palette_index;
    index += stride;
  }
}

const BuiltinMacro BUILTIN_MACROS[] = {
    {"sphere", builtin_macro_sphere},
    {"cuboid", builtin_macro_cuboid},
    {"line", builtin_macro_line},
    {"column", builtin_macro_column},
};

const int BUILTIN_MACROS_COUNT = sizeof(BUILTIN_MACROS) / sizeof(BuiltinMacro);

void register_builtin_macros(MacroRegistry *registry) {
  for (int i = 0; i < BUILTIN_MACROS_COUNT; i++) {
    macro_registry_register(registry, BUILTIN_MACROS[i].name, BUILTIN_MACROS[i].generator);
  }
}
