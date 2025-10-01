#include "builtin_functions.h"

#include <math.h>
#include <stddef.h>

static double fn_min(const double *args, size_t count) {
  if (count == 0) {
    return 0.0;
  }
  double result = args[0];
  for (size_t i = 1; i < count; i++) {
    result = fmin(result, args[i]);
  }
  return result;
}

static double fn_max(const double *args, size_t count) {
  if (count == 0) {
    return 0.0;
  }
  double result = args[0];
  for (size_t i = 1; i < count; i++) {
    result = fmax(result, args[i]);
  }
  return result;
}

static double fn_floor(const double *args, size_t count) {
  (void)count;
  return floor(args[0]);
}

static double fn_ceil(const double *args, size_t count) {
  (void)count;
  return ceil(args[0]);
}

static double fn_abs(const double *args, size_t count) {
  (void)count;
  return fabs(args[0]);
}

static double fn_clamp(const double *args, size_t count) {
  (void)count;
  double value = args[0];
  double min_value = args[1];
  double max_value = args[2];
  if (min_value > max_value) {
    double tmp = min_value;
    min_value = max_value;
    max_value = tmp;
  }
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}

static double fn_step(const double *args, size_t count) {
  (void)count;
  double edge = args[0];
  double x = args[1];
  return x < edge ? 0.0 : 1.0;
}

static double fn_sin(const double *args, size_t count) {
  (void)count;
  return sin(args[0]);
}

static double fn_cos(const double *args, size_t count) {
  (void)count;
  return cos(args[0]);
}

static double fn_tan(const double *args, size_t count) {
  (void)count;
  return tan(args[0]);
}

static double fn_sqrt(const double *args, size_t count) {
  (void)count;
  return sqrt(args[0]);
}

static double fn_pow(const double *args, size_t count) {
  (void)count;
  return pow(args[0], args[1]);
}

const BuiltinFunctionSpec BUILTIN_FUNCTIONS[] = {
    {"min", 1, SIZE_MAX, fn_min},
    {"max", 1, SIZE_MAX, fn_max},
    {"floor", 1, 1, fn_floor},
    {"ceil", 1, 1, fn_ceil},
    {"abs", 1, 1, fn_abs},
    {"clamp", 3, 3, fn_clamp},
    {"step", 2, 2, fn_step},
    {"sin", 1, 1, fn_sin},
    {"cos", 1, 1, fn_cos},
    {"tan", 1, 1, fn_tan},
    {"sqrt", 1, 1, fn_sqrt},
    {"pow", 2, 2, fn_pow},
};

const size_t BUILTIN_FUNCTION_COUNT = sizeof(BUILTIN_FUNCTIONS) / sizeof(BuiltinFunctionSpec);

void register_builtin_functions(FunctionRegistry *registry) {
  if (!registry) {
    return;
  }

  for (size_t i = 0; i < BUILTIN_FUNCTION_COUNT; i++) {
    function_registry_register(registry,
                               BUILTIN_FUNCTIONS[i].name,
                               BUILTIN_FUNCTIONS[i].min_args,
                               BUILTIN_FUNCTIONS[i].max_args,
                               BUILTIN_FUNCTIONS[i].function);
  }
}
