#include "builtin_occurrences.h"
#define FNL_IMPL
#include "FastNoiseLite.h"

#include <math.h>

static void compute_iteration_bounds(int origin, int step, int range_min, int range_max, int *start, int *end) {
  if (!start || !end) {
    return;
  }

  if (step == 0) {
    *start = 0;
    *end = 0;
    return;
  }

  double step_d = (double)step;
  double n1 = (range_min - origin) / step_d;
  double n2 = (range_max - origin) / step_d;
  double n_min = fmin(n1, n2);
  double n_max = fmax(n1, n2);
  int min_val = (int)ceil(n_min);
  int max_val = (int)floor(n_max);

  if (min_val > max_val) {
    *start = 0;
    *end = -1;
  } else {
    *start = min_val;
    *end = max_val;
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

  int range_min_x = runtime->base_x - SEARCH_MARGIN;
  int range_max_x = runtime->base_x + 15 + SEARCH_MARGIN;
  int range_min_y = runtime->base_y - SEARCH_MARGIN;
  int range_max_y = runtime->base_y + 15 + SEARCH_MARGIN;
  int range_min_z = runtime->base_z - SEARCH_MARGIN;
  int range_max_z = runtime->base_z + 15 + SEARCH_MARGIN;

  int kx_start, kx_end, ky_start, ky_end, kz_start, kz_end;
  compute_iteration_bounds(origin_x, step_x, range_min_x, range_max_x, &kx_start, &kx_end);
  compute_iteration_bounds(origin_y, step_y, range_min_y, range_max_y, &ky_start, &ky_end);
  compute_iteration_bounds(origin_z, step_z, range_min_z, range_max_z, &kz_start, &kz_end);

  for (int kx = kx_start; kx <= kx_end; kx++) {
    int anchor_x = origin_x + step_x * kx;
    for (int ky = ky_start; ky <= ky_end; ky++) {
      int anchor_y = origin_y + step_y * ky;
      for (int kz = kz_start; kz <= kz_end; kz++) {
        int anchor_z = origin_z + step_z * kz;
        runtime->run_body(runtime->userdata, body, anchor_x, anchor_y, anchor_z);
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
  int range_min_x = runtime->base_x - SEARCH_MARGIN;
  int range_max_x = runtime->base_x + 15 + SEARCH_MARGIN;
  int range_min_z = runtime->base_z - SEARCH_MARGIN;
  int range_max_z = runtime->base_z + 15 + SEARCH_MARGIN;

  // Sample noise for each XZ coordinate
  for (int x = range_min_x; x <= range_max_x; x++) {
    for (int z = range_min_z; z <= range_max_z; z++) {
      float world_x = (float)(x + origin_x);
      float world_z = (float)(z + origin_z);

      // Get noise value (-1 to 1)
      float noise_value = fnlGetNoise2D(&noise, world_x, world_z);

      // Check threshold if provided (normalize to 0-1 for comparison)
      if (threshold >= 0.0f) {
        float normalized_noise = (noise_value + 1.0f) * 0.5f;
        if (normalized_noise < threshold) {
          continue; // Skip this position
        }
      }

      // Calculate Y position
      int y_position = origin_y;
      if (amplitude != 0.0f) {
        int height_offset = (int)roundf(noise_value * amplitude);
        y_position = base_y + height_offset + origin_y;
      }

      // Run the body at this position
      runtime->run_body(runtime->userdata, body, x + origin_x, y_position, z + origin_z);
    }
  }
}

const BuiltinOccurrence BUILTIN_OCCURRENCES[] = {
    {"every", occurrence_every},
    {"noise", occurrence_noise},
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
