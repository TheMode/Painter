#pragma once

#include "painter.h"
#include <stddef.h>

typedef struct {
  const char *name;
  size_t min_args;
  size_t max_args;
  BuiltinFunction function;
} BuiltinFunctionSpec;

extern const BuiltinFunctionSpec BUILTIN_FUNCTIONS[];
extern const size_t BUILTIN_FUNCTION_COUNT;

void register_builtin_functions(FunctionRegistry *registry);
