#include "builtin_functions.h"

#include <math.h>
#include <stddef.h>

#define FN_UNARY(name, func)                                                                                                               \
  static double fn_##name(const double *args, size_t count) {                                                                              \
    (void)count;                                                                                                                           \
    return func(args[0]);                                                                                                                  \
  }

#define FN_BINARY(name, func)                                                                                                              \
  static double fn_##name(const double *args, size_t count) {                                                                              \
    (void)count;                                                                                                                           \
    return func(args[0], args[1]);                                                                                                         \
  }

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

FN_UNARY(floor, floor)
FN_UNARY(ceil, ceil)
FN_UNARY(round, round)
FN_UNARY(trunc, trunc)
FN_UNARY(abs, fabs)

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
  return args[1] < args[0] ? 0.0 : 1.0;
}

static double fn_mod(const double *args, size_t count) {
  (void)count;
  double divisor = args[1];
  if (divisor == 0.0) {
    return 0.0;
  }
  double result = fmod(args[0], divisor);
  if (result == 0.0) {
    return 0.0;
  }
  if ((result < 0.0 && divisor > 0.0) || (result > 0.0 && divisor < 0.0)) {
    result += divisor;
  }
  return result;
}

FN_UNARY(sin, sin)
FN_UNARY(cos, cos)
FN_UNARY(tan, tan)
FN_UNARY(asin, asin)
FN_UNARY(acos, acos)
FN_UNARY(atan, atan)
FN_BINARY(atan2, atan2)
FN_UNARY(sqrt, sqrt)
FN_BINARY(pow, pow)

static double fn_sum(const double *args, size_t count) {
  double total = 0.0;
  for (size_t i = 0; i < count; i++) {
    total += args[i];
  }
  return total;
}

static double fn_avg(const double *args, size_t count) {
  if (count == 0) {
    return 0.0;
  }
  return fn_sum(args, count) / (double)count;
}

static double fn_product(const double *args, size_t count) {
  double result = 1.0;
  for (size_t i = 0; i < count; i++) {
    result *= args[i];
  }
  return result;
}

FN_UNARY(log, log)
FN_UNARY(log10, log10)
FN_UNARY(exp, exp)

static double fn_between(const double *args, size_t count) {
  (void)count;
  double value = args[0];
  double min_value = args[1];
  double max_value = args[2];
  if (min_value > max_value) {
    double tmp = min_value;
    min_value = max_value;
    max_value = tmp;
  }
  return (value >= min_value && value <= max_value) ? 1.0 : 0.0;
}

static double fn_equal(const double *args, size_t count) {
  double tolerance = 1e-9;
  if (count >= 3) {
    tolerance = fabs(args[2]);
  }
  return fabs(args[0] - args[1]) <= tolerance ? 1.0 : 0.0;
}

const BuiltinFunctionSpec BUILTIN_FUNCTIONS[] = {
    {"min", 1, SIZE_MAX, fn_min},
    {"max", 1, SIZE_MAX, fn_max},
    {"floor", 1, 1, fn_floor},
    {"ceil", 1, 1, fn_ceil},
    {"round", 1, 1, fn_round},
    {"trunc", 1, 1, fn_trunc},
    {"abs", 1, 1, fn_abs},
    {"clamp", 3, 3, fn_clamp},
    {"step", 2, 2, fn_step},
    {"mod", 2, 2, fn_mod},
    {"sum", 1, SIZE_MAX, fn_sum},
    {"avg", 1, SIZE_MAX, fn_avg},
    {"product", 1, SIZE_MAX, fn_product},
    {"sin", 1, 1, fn_sin},
    {"cos", 1, 1, fn_cos},
    {"tan", 1, 1, fn_tan},
    {"asin", 1, 1, fn_asin},
    {"acos", 1, 1, fn_acos},
    {"atan", 1, 1, fn_atan},
    {"atan2", 2, 2, fn_atan2},
    {"log", 1, 1, fn_log},
    {"log10", 1, 1, fn_log10},
    {"exp", 1, 1, fn_exp},
    {"sqrt", 1, 1, fn_sqrt},
    {"pow", 2, 2, fn_pow},
    {"between", 3, 3, fn_between},
    {"equal", 2, 3, fn_equal},
};

const size_t BUILTIN_FUNCTION_COUNT = sizeof(BUILTIN_FUNCTIONS) / sizeof(BuiltinFunctionSpec);

void register_builtin_functions(FunctionRegistry *registry) {
  if (!registry) {
    return;
  }

  for (size_t i = 0; i < BUILTIN_FUNCTION_COUNT; i++) {
    function_registry_register(
        registry, BUILTIN_FUNCTIONS[i].name, BUILTIN_FUNCTIONS[i].min_args, BUILTIN_FUNCTIONS[i].max_args, BUILTIN_FUNCTIONS[i].function);
  }
}
