#include "builtin_occurrences.h"
#include "painter_eval.h"
#define FNL_IMPL
#include "FastNoiseLite.h"

#include <math.h>
#include <stdint.h>

static int floor_div_int(int numerator, int denominator) {
  int64_t dividend = numerator;
  int64_t divisor = denominator;

  int64_t quotient = dividend / divisor;
  int64_t remainder = dividend % divisor;
  if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
    quotient -= 1;
  }
  return (int)quotient;
}

static int ceil_div_int(int numerator, int denominator) {
  int64_t dividend = numerator;
  int64_t divisor = denominator;

  int64_t quotient = dividend / divisor;
  int64_t remainder = dividend % divisor;
  if (remainder != 0 && ((remainder < 0) == (divisor < 0))) {
    quotient += 1;
  }
  return (int)quotient;
}

static void compute_iteration_bounds(int origin, int step, int range_min, int range_max, int *start, int *end) {
  if (!start || !end) {
    return;
  }

  if (step == 0) {
    *start = 0;
    *end = 0;
    return;
  }

  if (step > 0) {
    const int lower = ceil_div_int(range_min - origin, step);
    const int upper = floor_div_int(range_max - origin, step);
    if (lower > upper) {
      *start = 0;
      *end = -1;
    } else {
      *start = lower;
      *end = upper;
    }
    return;
  }

  const int lower = ceil_div_int(range_max - origin, step);
  const int upper = floor_div_int(range_min - origin, step);
  if (lower > upper) {
    *start = 0;
    *end = -1;
  } else {
    *start = lower;
    *end = upper;
  }
}

static void occurrence_every(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y, int origin_z,
    OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  // Get named arguments: .x, .y, .z for step sizes
  Expression *step_x_expr = named_arg_get(args, "x");
  Expression *step_y_expr = named_arg_get(args, "y");
  Expression *step_z_expr = named_arg_get(args, "z");

  if (!step_x_expr || !step_y_expr || !step_z_expr) {
    return; // Missing required arguments
  }

  int step_x = (int)llround(painter_evaluate_expression(step_x_expr, state));
  int step_y = (int)llround(painter_evaluate_expression(step_y_expr, state));
  int step_z = (int)llround(painter_evaluate_expression(step_z_expr, state));

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

  int kx_start, kx_end, ky_start, ky_end, kz_start, kz_end;
  compute_iteration_bounds(origin_x, step_x, range_min_x, range_max_x, &kx_start, &kx_end);
  compute_iteration_bounds(origin_y, step_y, range_min_y, range_max_y, &ky_start, &ky_end);
  compute_iteration_bounds(origin_z, step_z, range_min_z, range_max_z, &kz_start, &kz_end);

  if (kx_start > kx_end || ky_start > ky_end || kz_start > kz_end) {
    return;
  }

  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  void *userdata = runtime->userdata;

  int anchor_x = origin_x + step_x * kx_start;
  for (int kx = kx_start; kx <= kx_end; ++kx, anchor_x += step_x) {
    int anchor_y = origin_y + step_y * ky_start;
    for (int ky = ky_start; ky <= ky_end; ++ky, anchor_y += step_y) {
      int anchor_z = origin_z + step_z * kz_start;
      for (int kz = kz_start; kz <= kz_end; ++kz, anchor_z += step_z) {
        run_body(userdata, body, anchor_x, anchor_y, anchor_z);
      }
    }
  }
}

// @noise .frequency=<val> .seed=<val> [.threshold=<val>] [.amplitude=<val>] [.base_y=<val>]
// General-purpose noise-based occurrence
// - frequency: controls feature density/smoothness (e.g., 0.01-0.1)
// - seed: random seed for reproducible patterns
// - threshold: optional, if provided the body only executes if noise > threshold (0-1)
// - amplitude: optional, if provided noise is multiplied and added to Y coordinate (for terrain height)
// - base_y: optional, base Y level when using amplitude (default 0)
//
// Usage examples:
// @noise .frequency=0.05 .seed=12345 .threshold=0.7 { ... }              // Sparse placement (30% of area)
// @noise .frequency=0.02 .seed=12345 .amplitude=16 .base_y=64 { ... }    // Terrain generation at y=64±16
// @noise .frequency=0.1 .seed=12345 .threshold=0.8 .amplitude=16 .base_y=64 { ... }  // Trees on terrain (20% coverage)
static void occurrence_noise(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y, int origin_z,
    OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  // Get named arguments
  Expression *frequency_expr = named_arg_get(args, "frequency");
  Expression *seed_expr = named_arg_get(args, "seed");
  Expression *threshold_expr = named_arg_get(args, "threshold");
  Expression *amplitude_expr = named_arg_get(args, "amplitude");
  Expression *base_y_expr = named_arg_get(args, "base_y");

  if (!frequency_expr || !seed_expr) {
    return; // Missing required arguments
  }

  float frequency = (float)painter_evaluate_expression(frequency_expr, state);
  int seed = (int)llround(painter_evaluate_expression(seed_expr, state));
  float threshold = threshold_expr ? (float)painter_evaluate_expression(threshold_expr, state) : -1.0f;
  float amplitude = amplitude_expr ? (float)painter_evaluate_expression(amplitude_expr, state) : 0.0f;
  int base_y = base_y_expr ? (int)llround(painter_evaluate_expression(base_y_expr, state)) : 0;

  // Initialize noise
  fnl_state noise = fnlCreateState();
  noise.seed = seed;
  noise.frequency = frequency;
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

  const int SEARCH_MARGIN = 16;
  const int base_x = runtime->base_x;
  const int base_z = runtime->base_z;

  const int range_min_x = base_x - SEARCH_MARGIN;
  const int range_max_x = base_x + 15 + SEARCH_MARGIN;
  const int range_min_z = base_z - SEARCH_MARGIN;
  const int range_max_z = base_z + 15 + SEARCH_MARGIN;

  // Sample noise for each XZ coordinate
  const bool apply_threshold = threshold >= 0.0f;
  const bool apply_amplitude = amplitude != 0.0f;
  const float threshold_value = threshold;
  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  void *userdata = runtime->userdata;

  const int origin_offset_y = origin_y;
  const int amplitude_base_y = base_y + origin_y;

  for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
    for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
      const float noise_value = fnlGetNoise2D(&noise, (float)anchor_x, (float)anchor_z);

      if (apply_threshold) {
        const float normalized_noise = (noise_value + 1.0f) * 0.5f;
        if (normalized_noise < threshold_value) {
          continue;
        }
      }

      int anchor_y = origin_offset_y;
      if (apply_amplitude) {
        const int height_offset = (int)lroundf(noise_value * amplitude);
        anchor_y = amplitude_base_y + height_offset;
      }

      run_body(userdata, body, anchor_x, anchor_y, anchor_z);
    }
  }
}

// @noise3d .frequency=<val> .seed=<val> [.threshold=<val>]
// Samples 3D noise and executes the body at positions that satisfy the threshold
static void occurrence_noise3d(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y, int origin_z,
    OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  Expression *frequency_expr = named_arg_get(args, "frequency");
  Expression *seed_expr = named_arg_get(args, "seed");
  Expression *threshold_expr = named_arg_get(args, "threshold");

  if (!frequency_expr || !seed_expr) {
    return;
  }

  const float frequency = (float)painter_evaluate_expression(frequency_expr, state);
  const int seed = (int)llround(painter_evaluate_expression(seed_expr, state));
  const float threshold = threshold_expr ? (float)painter_evaluate_expression(threshold_expr, state) : -1.0f;

  fnl_state noise = fnlCreateState();
  noise.seed = seed;
  noise.frequency = frequency;
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

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

  const bool apply_threshold = threshold >= 0.0f;
  const float threshold_value = threshold;

  void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
  void *userdata = runtime->userdata;

  for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
    for (int y = range_min_y, anchor_y = y + origin_y; y <= range_max_y; ++y, ++anchor_y) {
      for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
        const float noise_value = fnlGetNoise3D(&noise, (float)anchor_x, (float)anchor_y, (float)anchor_z);

        if (apply_threshold) {
          const float normalized_noise = (noise_value + 1.0f) * 0.5f;
          if (normalized_noise < threshold_value) {
            continue;
          }
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
static void occurrence_section(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y, int origin_z,
    OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0) {
    return;
  }

  // Get optional named arguments: .x, .y, .z for offsets within the section
  Expression *offset_x_expr = named_arg_get(args, "x");
  Expression *offset_y_expr = named_arg_get(args, "y");
  Expression *offset_z_expr = named_arg_get(args, "z");

  int offset_x = offset_x_expr ? (int)llround(painter_evaluate_expression(offset_x_expr, state)) : 0;
  int offset_y = offset_y_expr ? (int)llround(painter_evaluate_expression(offset_y_expr, state)) : 0;
  int offset_z = offset_z_expr ? (int)llround(painter_evaluate_expression(offset_z_expr, state)) : 0;

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
    {"noise", occurrence_noise},
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
