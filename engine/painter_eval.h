#pragma once
#include "painter.h"

double painter_evaluate_expression(const Expression *expr, ExecutionState *state);
void process_instruction(Instruction *instr, ExecutionState *state, int origin_x, int origin_y, int origin_z);
Section *generate_section(Program *program, int section_x, int section_y, int section_z);
void section_free(Section *section);

// Palette helpers shared between the interpreter and macros
int painter_palette_get_or_add(ExecutionState *state, const char *block_string);
bool painter_section_contains_point(const ExecutionState *state, int x, int y, int z);
bool painter_section_clip_aabb(const ExecutionState *state, PainterAABB *box);
void painter_format_block(char *buffer, size_t buffer_size, const char *block_name, const char *block_properties);
