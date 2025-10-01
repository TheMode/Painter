#pragma once

#include "painter.h"
#include <stddef.h>

typedef struct {
  const char *name;
  OccurrenceGenerator generator;
} BuiltinOccurrence;

extern const BuiltinOccurrence BUILTIN_OCCURRENCES[];
extern const size_t BUILTIN_OCCURRENCE_COUNT;

void register_builtin_occurrences(OccurrenceTypeRegistry *registry);
