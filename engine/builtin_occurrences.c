#include "builtin_occurrences.h"

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

static void occurrence_every(const double *args, size_t arg_count, const InstructionList *body, int origin_x, int origin_y, int origin_z,
    OccurrenceRuntime *runtime) {
  if (!runtime || !runtime->run_body || !body || body->count == 0 || arg_count < 3) {
    return;
  }

  int step_x = (int)llround(args[0]);
  int step_y = (int)llround(args[1]);
  int step_z = (int)llround(args[2]);

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

const BuiltinOccurrence BUILTIN_OCCURRENCES[] = {
    {"every", occurrence_every},
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
