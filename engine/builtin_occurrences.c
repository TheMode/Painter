#include "builtin_occurrences.h"
#include "painter_eval.h"
#define FNL_IMPL
#include "FastNoiseLite.h"

#include <math.h>
#include <stdint.h>

static fnl_state *noise_mutate(ExecutionState *state, int seed, float frequency) {
  fnl_state *n = (fnl_state *)state->noise_state;
  if (!n) {
    return NULL;
  }
  n->seed = seed;
  n->frequency = frequency;
  n->noise_type = FNL_NOISE_OPENSIMPLEX2;
  return n;
}

static int floor_div_int(int numerator, int denominator) {
  const int64_t dividend = numerator;
  const int64_t divisor = denominator;

  int64_t quotient = dividend / divisor;
  const int64_t remainder = dividend % divisor;
  if (remainder && ((remainder < 0) != (divisor < 0))) {
    --quotient;
  }
  return (int)quotient;
}

static int ceil_div_int(int numerator, int denominator) {
  const int64_t dividend = numerator;
  const int64_t divisor = denominator;

  int64_t quotient = dividend / divisor;
  const int64_t remainder = dividend % divisor;
  if (remainder && ((remainder < 0) == (divisor < 0))) {
    ++quotient;
  }
  return (int)quotient;
}

static bool compute_iteration_bounds(int step, int range_min, int range_max, int *start, int *end) {
  if (!start || !end) {
    return false;
  }

  if (step == 0) {
    *start = *end = 0;
    return true;
  }

  int lower;
  int upper;
  if (step > 0) {
    lower = ceil_div_int(range_min, step);
    upper = floor_div_int(range_max, step);
  } else {
    lower = ceil_div_int(range_max, step);
    upper = floor_div_int(range_min, step);
  }

  if (lower > upper) {
    return false;
  }

  *start = lower;
  *end = upper;
  return true;
}

static void occurrence_every(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y,

    int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !body || body->count == 0 || !args) {
    return;
  }

  const int step_x = painter_eval_int_or(named_arg_get(args, "x"), state, 0);
  const int step_y = painter_eval_int_or(named_arg_get(args, "y"), state, 0);
  const int step_z = painter_eval_int_or(named_arg_get(args, "z"), state, 0);

  const int SEARCH_MARGIN = 16;
  const int base_x = runtime->base_x;
  const int base_y = runtime->base_y;
  const int base_z = runtime->base_z;

  const int range_min_x = base_x - SEARCH_MARGIN;
  const int range_max_x = base_x + 15 + SEARCH_MARGIN;
  const int range_min_y = base_y - SEARCH_MARGIN;
  const int range_max_y = base_y + 15 + SEARCH_MARGIN;
  const int range_min_z = base_z - SEARCH_MARGIN;
  const int range_max_z = base_z + 15 + SEARCH_MARGIN;

  /* The repetition grid for @every is defined relative to a global pattern
   * origin (program origin). We compute anchor world-coordinates from that
   * global origin and, for each section generation, convert them to local
   * anchors relative to the section base so only anchors that fall into the
   * current section are executed. Using a fixed pattern origin prevents the
   * repetition grid from shifting per-section (which previously caused a
   * copy of the tower in every section). */
  int kx_start, kx_end, ky_start, ky_end, kz_start, kz_end;
  if (!compute_iteration_bounds(step_x, range_min_x, range_max_x, &kx_start, &kx_end) ||
      !compute_iteration_bounds(step_y, range_min_y, range_max_y, &ky_start, &ky_end) ||
      !compute_iteration_bounds(step_z, range_min_z, range_max_z, &kz_start, &kz_end)) {
    return;
  }

  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  if (!run_body) {
    return;
  }
  void *userdata = runtime->userdata;

  for (int kx = kx_start, anchor_world_x = step_x * kx_start; kx <= kx_end; ++kx, anchor_world_x += step_x) {
    for (int ky = ky_start, anchor_world_y = step_y * ky_start; ky <= ky_end; ++ky, anchor_world_y += step_y) {
      int anchor_world_z = step_z * kz_start;
      for (int kz = kz_start; kz <= kz_end; ++kz, anchor_world_z += step_z) {
        run_body(userdata, body, anchor_world_x, anchor_world_y, anchor_world_z);
      }
    }
  }
}

// @noise2d .frequency=<val> .seed=<val> [.threshold=<val>] [.spread=<val>] [.y=<val>]
// 2D noise sampler that scans the XZ plane and executes the body at each anchor
// - frequency: controls feature density/smoothness (e.g., 0.01-0.1)
// - seed: random seed for reproducible patterns
// - threshold: optional 0-1 cutoff; only run body when noise >= threshold
// - spread: optional vertical displacement multiplier applied to the noise value
// - y: optional base Y level (defaults to the origin)
//
// Usage examples:
// @noise2d .frequency=0.05 .seed=12345 .threshold=0.7 { ... }                // Sparse surface placement
// @noise2d .frequency=0.02 .seed=12345 .spread=16 .y=64 { ... }              // Terrain heightfield centered at y=64
// @noise2d .frequency=0.1 .seed=98765 .spread=12 .y=32 .threshold=0.8 { ... } // Clustered ore veins
static void occurrence_noise2d(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x,
    int origin_y, int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  const Expression *frequency_expr = named_arg_get(args, "frequency");
  const Expression *seed_expr = named_arg_get(args, "seed");
  if (!frequency_expr || !seed_expr) {
    return;
  }

  const Expression *threshold_expr = named_arg_get(args, "threshold");
  const Expression *spread_expr = named_arg_get(args, "spread");
  const Expression *y_expr = named_arg_get(args, "y");

  const float frequency = (float)painter_eval_double_or(frequency_expr, state, 0.0);
  const int seed = painter_eval_int_or(seed_expr, state, 0);
  const float threshold_value = (float)painter_eval_double_or(threshold_expr, state, -1.0);
  const float spread = (float)painter_eval_double_or(spread_expr, state, 0.0);
  const int base_y = painter_eval_int_or(y_expr, state, origin_y);

  const bool use_threshold = threshold_expr && threshold_value >= 0.0f;
  const float threshold_cutoff = (threshold_value * 2.0f) - 1.0f;
  const bool use_spread = spread_expr && spread != 0.0f;

  fnl_state *noise = noise_mutate(state, seed, frequency);
  if (!noise) {
    return;
  }

  const int SEARCH_MARGIN = 16;
  const int base_x = runtime->base_x;
  const int base_z = runtime->base_z;

  const int range_min_x = base_x - SEARCH_MARGIN;
  const int range_max_x = base_x + 15 + SEARCH_MARGIN;
  const int range_min_z = base_z - SEARCH_MARGIN;
  const int range_max_z = base_z + 15 + SEARCH_MARGIN;

  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  void *userdata = runtime->userdata;

  for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
    for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
      const float noise_value = fnlGetNoise2D(noise, (float)anchor_x, (float)anchor_z);

      if (use_threshold && noise_value < threshold_cutoff) {
        continue;
      }

      int anchor_y = base_y;
      if (use_spread) {
        anchor_y += (int)lroundf(noise_value * spread);
      }

      run_body(userdata, body, anchor_x, anchor_y, anchor_z);
    }
  }
}

// @noise3d .frequency=<val> .seed=<val> [.threshold=<val>] [.min_y=<val>] [.max_y=<val>]
// 3D noise sampler that scans the full XYZ volume, useful for caves, fluids, etc.
// - frequency: controls feature density/smoothness
// - seed: random seed for reproducible patterns
// - threshold: optional 0-1 cutoff; only run body when noise >= threshold
// - min_y / max_y: optional inclusive world-height bounds to constrain sampling
//
// Usage examples:
// @noise3d .frequency=0.08 .seed=424242 .threshold=0.95 { ... }               // Sparse cave pockets
// @noise3d .frequency=0.12 .seed=11235 .min_y=24 .max_y=56 { ... }            // Ores across a vertical band
static void occurrence_noise3d(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x,
    int origin_y, int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  const Expression *frequency_expr = named_arg_get(args, "frequency");
  const Expression *seed_expr = named_arg_get(args, "seed");
  if (!frequency_expr || !seed_expr) {
    return;
  }

  const Expression *threshold_expr = named_arg_get(args, "threshold");
  const Expression *min_y_expr = named_arg_get(args, "min_y");
  const Expression *max_y_expr = named_arg_get(args, "max_y");

  const float frequency = (float)painter_eval_double_or(frequency_expr, state, 0.0);
  const int seed = painter_eval_int_or(seed_expr, state, 0);
  const float threshold_value = (float)painter_eval_double_or(threshold_expr, state, -1.0);
  const bool use_threshold = threshold_expr && threshold_value >= 0.0f;
  const float threshold_cutoff = (threshold_value * 2.0f) - 1.0f;
  const bool clamp_min_y = min_y_expr != NULL;
  const bool clamp_max_y = max_y_expr != NULL;
  const int min_y = clamp_min_y ? painter_eval_int_or(min_y_expr, state, INT32_MIN) : INT32_MIN;
  const int max_y = clamp_max_y ? painter_eval_int_or(max_y_expr, state, INT32_MAX) : INT32_MAX;
  if (clamp_min_y && clamp_max_y && min_y > max_y) {
    return;
  }

  fnl_state *noise = noise_mutate(state, seed, frequency);
  if (!noise) {
    return;
  }

  const int SEARCH_MARGIN = 16;
  const int base_x = runtime->base_x;
  const int base_y = runtime->base_y;
  const int base_z = runtime->base_z;

  const int range_min_x = base_x - SEARCH_MARGIN;
  const int range_max_x = base_x + 15 + SEARCH_MARGIN;
  int range_min_y = base_y - SEARCH_MARGIN;
  int range_max_y = base_y + 15 + SEARCH_MARGIN;
  const int range_min_z = base_z - SEARCH_MARGIN;
  const int range_max_z = base_z + 15 + SEARCH_MARGIN;

  if (clamp_min_y) {
    const int min_bound = min_y - origin_y;
    if (min_bound > range_min_y) {
      range_min_y = min_bound;
    }
  }
  if (clamp_max_y) {
    const int max_bound = max_y - origin_y;
    if (max_bound < range_max_y) {
      range_max_y = max_bound;
    }
  }
  if (range_min_y > range_max_y) {
    return;
  }

  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  void *userdata = runtime->userdata;

  for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
    for (int y = range_min_y, anchor_y = y + origin_y; y <= range_max_y; ++y, ++anchor_y) {
      for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
        const float noise_value = fnlGetNoise3D(noise, (float)anchor_x, (float)anchor_y, (float)anchor_z);

        if (use_threshold && noise_value < threshold_cutoff) {
          continue;
        }

        run_body(userdata, body, anchor_x, anchor_y, anchor_z);
      }
    }
  }
}

// @section [.x=<val>] [.y=<val>] [.z=<val>]
// Triggers once per section at the specified offset within the section
// Offsets default to 0 if not provided
// Sections are 16x16x16 blocks
static void occurrence_section(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x,
    int origin_y, int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !body || body->count == 0) {
    return;
  }

  const int offset_x = painter_eval_int_or(named_arg_get(args, "x"), state, 0);
  const int offset_y = painter_eval_int_or(named_arg_get(args, "y"), state, 0);
  const int offset_z = painter_eval_int_or(named_arg_get(args, "z"), state, 0);

  // Validate offsets are within section bounds (0-15)
  if (offset_x < 0 || offset_x > 15 || offset_y < 0 || offset_y > 15 || offset_z < 0 || offset_z > 15) {
    return; // Invalid offsets
  }

  // Calculate the world position for this section occurrence
  // Section coordinates are the base coordinates of the current section
  const int anchor_x = runtime->base_x + offset_x;
  const int anchor_y = runtime->base_y + offset_y;
  const int anchor_z = runtime->base_z + offset_z;

  // Execute the body once at the calculated position
  runtime->run_body(runtime->userdata, body, anchor_x, anchor_y, anchor_z);
}

const BuiltinOccurrence BUILTIN_OCCURRENCES[] = {
    {"every", occurrence_every},
    {"noise2d", occurrence_noise2d},
    {"noise3d", occurrence_noise3d},
    {"section", occurrence_section},
};

const size_t BUILTIN_OCCURRENCE_COUNT = sizeof(BUILTIN_OCCURRENCES) / sizeof(BuiltinOccurrence);

void register_builtin_occurrences(OccurrenceTypeRegistry *registry) {
  if (!registry) {
    return;
  }

  for (size_t i = 0; i < BUILTIN_OCCURRENCE_COUNT; i++) {
    occurrence_type_registry_register(registry, BUILTIN_OCCURRENCES[i].name, BUILTIN_OCCURRENCES[i].generator);
  }
}
