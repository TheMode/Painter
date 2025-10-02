#pragma once

#include "painter.h"

// Internal evaluation functions (not exposed to external users)
// These are used internally for executing instructions and generating sections

// Evaluate an expression and return its numeric value
double painter_evaluate_expression(const Expression *expr, ExecutionState *state);

// Process a single instruction at the given origin coordinates
void process_instruction(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z);

// Main section generation entry point
Section *generate_section(Program *program, int section_x, int section_y, int section_z);

// Section cleanup
void section_free(Section *section);
