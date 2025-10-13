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

int painter_eval_offset(const Expression *expr, ExecutionState *state);
bool painter_eval_positive_extent(const Expression *expr, ExecutionState *state, int *out_extent);
bool painter_eval_boolean_flag(const Expression *expr, ExecutionState *state);
bool painter_eval_coordinate_argument(const Expression *expr, ExecutionState *state, int *out_x, int *out_y, int *out_z);
bool painter_eval_positive_component_or_default(const Expression *expr, ExecutionState *state, int default_value, int *out_value);
bool painter_eval_positive_vector3(const Expression *expr, ExecutionState *state, int default_y, int default_z, int *out_x, int *out_y, int *out_z);
double painter_eval_double_or(const Expression *expr, ExecutionState *state, double fallback);
int painter_eval_int_or(const Expression *expr, ExecutionState *state, int fallback);
