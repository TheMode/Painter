#pragma once

#include "painter.h"

typedef struct {
  const char *name;
  MacroGenerator generator;
} BuiltinMacro;

extern const BuiltinMacro BUILTIN_MACROS[];
extern const int BUILTIN_MACROS_COUNT;

void register_builtin_macros(MacroRegistry *registry);
