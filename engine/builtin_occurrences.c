#include "builtin_occurrences.h"
#include "painter_eval.h"
#define FNL_IMPL
#include "FastNoiseLite.h"

#include <math.h>
#include <stdint.h>

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

static double eval_double_or(const Expression *expr, ExecutionState *state, double fallback) {
  return expr ? painter_evaluate_expression(expr, state) : fallback;
}

static int eval_int_or(const Expression *expr, ExecutionState *state, int fallback) {
  return (int)llround(eval_double_or(expr, state, (double)fallback));
}

static void occurrence_every(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y,
    int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  const Expression *step_x_expr = named_arg_get(args, "x");
  const Expression *step_y_expr = named_arg_get(args, "y");
  const Expression *step_z_expr = named_arg_get(args, "z");

  if (!step_x_expr || !step_y_expr || !step_z_expr) {
    return;
  }

  const int step_x = eval_int_or(step_x_expr, state, 0);
  const int step_y = eval_int_or(step_y_expr, state, 0);
  const int step_z = eval_int_or(step_z_expr, state, 0);

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
  void *userdata = runtime->userdata;

  for (int kx = kx_start; kx <= kx_end; ++kx) {
    const int anchor_world_x = step_x * kx;
    for (int ky = ky_start; ky <= ky_end; ++ky) {
      const int anchor_world_y = step_y * ky;
      for (int kz = kz_start; kz <= kz_end; ++kz) {
        const int anchor_world_z = step_z * kz;
        run_body(userdata, body, anchor_world_x, anchor_world_y, anchor_world_z);
      }
    }
  }
}

// @noise .frequency=<val> .seed=<val> [.dimensions=<2|3>] [.threshold=<val>] [.amplitude=<val>] [.base_y=<val>]
// General-purpose noise occurrence for both 2D (heightfields) and 3D (volume scattering)
// - dimensions: defaults to 2. When set to 3 the body is sampled across XYZ space.
// - frequency: controls feature density/smoothness (e.g., 0.01-0.1)
// - seed: random seed for reproducible patterns
// - threshold: optional, if provided the body only executes if noise > threshold (0-1)
// - amplitude: optional (2D mode only), scales vertical displacement around base_y
// - base_y: optional base Y level when using amplitude (default 0)
//
// Usage examples:
// @noise .frequency=0.05 .seed=12345 .threshold=0.7 { ... }              // Sparse placement (30% of area)
// @noise .frequency=0.02 .seed=12345 .amplitude=16 .base_y=64 { ... }    // Terrain generation at y=64±16
// @noise .frequency=0.04 .seed=424242 .dimensions=3 .threshold=0.95 { ... } // Volumetric scattering
static void occurrence_noise(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x, int origin_y,
    int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || !args) {
    return;
  }

  // Get named arguments
  const Expression *frequency_expr = named_arg_get(args, "frequency");
  const Expression *seed_expr = named_arg_get(args, "seed");
  const Expression *threshold_expr = named_arg_get(args, "threshold");
  const Expression *amplitude_expr = named_arg_get(args, "amplitude");
  const Expression *base_y_expr = named_arg_get(args, "base_y");
  const Expression *dimensions_expr = named_arg_get(args, "dimensions");

  if (!frequency_expr || !seed_expr) {
    return; // Missing required arguments
  }

  const float frequency = (float)eval_double_or(frequency_expr, state, 0.0);
  const int seed = eval_int_or(seed_expr, state, 0);
  const float threshold = (float)eval_double_or(threshold_expr, state, -1.0);
  const float amplitude = (float)eval_double_or(amplitude_expr, state, 0.0);
  const int base_y = eval_int_or(base_y_expr, state, 0);
  const int dimensions = eval_int_or(dimensions_expr, state, 2);

  // Initialize noise
  fnl_state noise = fnlCreateState();
  noise.seed = seed;
  noise.frequency = frequency;
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

  const int SEARCH_MARGIN = 16;
  const int base_x = runtime->base_x;
  if (dimensions == 2) {
    const int base_z = runtime->base_z;

    const int range_min_x = base_x - SEARCH_MARGIN;
    const int range_max_x = base_x + 15 + SEARCH_MARGIN;
    const int range_min_z = base_z - SEARCH_MARGIN;
    const int range_max_z = base_z + 15 + SEARCH_MARGIN;

    // Sample noise for each XZ coordinate
    const bool apply_threshold = threshold >= 0.0f;
    const bool apply_amplitude = amplitude != 0.0f;
    void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
    void *userdata = runtime->userdata;

    const int origin_offset_y = origin_y;
    const int amplitude_base_y = base_y + origin_y;
    const int base_anchor_y = apply_amplitude ? amplitude_base_y : origin_offset_y;

    for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
      for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
        const float noise_value = fnlGetNoise2D(&noise, (float)anchor_x, (float)anchor_z);

        if (apply_threshold) {
          const float normalized_noise = (noise_value + 1.0f) * 0.5f;
          if (normalized_noise < threshold) {
            continue;
          }
        }

        const int height_offset = apply_amplitude ? (int)lroundf(noise_value * amplitude) : 0;
        run_body(userdata, body, anchor_x, base_anchor_y + height_offset, anchor_z);
      }
    }
    return;
  }

  if (dimensions == 3) {
    const int base_y = runtime->base_y;
    const int base_z = runtime->base_z;

    const int range_min_x = base_x - SEARCH_MARGIN;
    const int range_max_x = base_x + 15 + SEARCH_MARGIN;
    const int range_min_y = base_y - SEARCH_MARGIN;
    const int range_max_y = base_y + 15 + SEARCH_MARGIN;
    const int range_min_z = base_z - SEARCH_MARGIN;
    const int range_max_z = base_z + 15 + SEARCH_MARGIN;

    const bool apply_threshold = threshold >= 0.0f;

    void (*run_body)(void *, const InstructionList *, int, int, int) = runtime->run_body;
    void *userdata = runtime->userdata;

    for (int x = range_min_x, anchor_x = x + origin_x; x <= range_max_x; ++x, ++anchor_x) {
      for (int y = range_min_y, anchor_y = y + origin_y; y <= range_max_y; ++y, ++anchor_y) {
        for (int z = range_min_z, anchor_z = z + origin_z; z <= range_max_z; ++z, ++anchor_z) {
          const float noise_value = fnlGetNoise3D(&noise, (float)anchor_x, (float)anchor_y, (float)anchor_z);

          if (apply_threshold) {
            const float normalized_noise = (noise_value + 1.0f) * 0.5f;
            if (normalized_noise < threshold) {
              continue;
            }
          }

          run_body(userdata, body, anchor_x, anchor_y, anchor_z);
        }
      }
    }
    return;
  }
}

// @section [.x=<val>] [.y=<val>] [.z=<val>]
// Triggers once per section at the specified offset within the section
// Offsets default to 0 if not provided
// Sections are 16x16x16 blocks
static void occurrence_section(ExecutionState *state, const NamedArgumentList *args, const InstructionList *body, int origin_x,
    int origin_y, int origin_z, OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0) {
    return;
  }

  const int offset_x = eval_int_or(named_arg_get(args, "x"), state, 0);
  const int offset_y = eval_int_or(named_arg_get(args, "y"), state, 0);
  const int offset_z = eval_int_or(named_arg_get(args, "z"), state, 0);

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
