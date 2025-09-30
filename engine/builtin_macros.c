#include "builtin_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double evaluate_expression(Expression *expr, VariableContext *ctx);

// Helper function to create a full block string (name + properties)
static void create_block_string(char *buffer, size_t buffer_size, const char *block_name, const char *block_properties) {
  if (block_properties && block_properties[0] != '\0') {
    snprintf(buffer, buffer_size, "%s[%s]", block_name, block_properties);
  } else {
    snprintf(buffer, buffer_size, "%s", block_name);
  }
}

// Helper function to find or add a block to the palette
static int get_palette_index(char ***palette, int *palette_size, int *palette_capacity, const char *block_string) {
  // Search for existing block in palette
  for (int i = 0; i < *palette_size; i++) {
    if (strcmp((*palette)[i], block_string) == 0) {
      return i;
    }
  }
  
  // Add new block to palette
  if (*palette_size >= *palette_capacity) {
    *palette_capacity *= 2;
    *palette = realloc(*palette, sizeof(char *) * (*palette_capacity));
  }
  
  (*palette)[*palette_size] = malloc(strlen(block_string) + 1);
  strcpy((*palette)[*palette_size], block_string);
  return (*palette_size)++;
}

// Helper function to evaluate an expression to a number
static double evaluate_expression(Expression *expr, VariableContext *ctx) {
  if (!expr) return 0.0;
  
  switch (expr->type) {
    case EXPR_NUMBER:
      return expr->number;
    case EXPR_IDENTIFIER:
      return context_get(ctx, expr->identifier);
    case EXPR_BINARY_OP: {
      double left = evaluate_expression(expr->binary.left, ctx);
      double right = evaluate_expression(expr->binary.right, ctx);
      switch (expr->binary.op) {
        case OP_ADD: return left + right;
        case OP_SUBTRACT: return left - right;
        case OP_MULTIPLY: return left * right;
        case OP_DIVIDE: return right != 0 ? left / right : 0;
        case OP_MODULO: return (int)left % (int)right;
        default: return 0.0;
      }
    }
    case EXPR_UNARY_OP: {
      double operand = evaluate_expression(expr->unary.operand, ctx);
      switch (expr->unary.op) {
        case OP_NEGATE: return -operand;
        default: return 0.0;
      }
    }
    default:
      return 0.0;
  }
}

// Sphere macro: #sphere .x=<x> .y=<y> .z=<z> .radius=<radius> .block=<block_name>
// Generates a filled 3D sphere
void builtin_macro_sphere(VariableContext *ctx, MacroArgument *args, int arg_count,
                         int base_x, int base_y, int base_z,
                         int *block_indices, char ***palette, 
                         int *palette_size, int *palette_capacity) {
  // Get arguments
  Expression *x_expr = macro_get_arg(args, arg_count, "x");
  Expression *y_expr = macro_get_arg(args, arg_count, "y");
  Expression *z_expr = macro_get_arg(args, arg_count, "z");
  Expression *radius_expr = macro_get_arg(args, arg_count, "radius");
  Expression *block_expr = macro_get_arg(args, arg_count, "block");
  
  if (!radius_expr || !block_expr) {
    return; // Missing required arguments
  }
  
  int center_x = x_expr ? (int)evaluate_expression(x_expr, ctx) : 0;
  int center_y = y_expr ? (int)evaluate_expression(y_expr, ctx) : 0;
  int center_z = z_expr ? (int)evaluate_expression(z_expr, ctx) : 0;
  int radius = (int)evaluate_expression(radius_expr, ctx);
  
  // Get block name (must be an identifier)
  if (block_expr->type != EXPR_IDENTIFIER) {
    return; // Block must be an identifier
  }
  
  char block_string[MAX_TOKEN_VALUE_LENGTH];
  create_block_string(block_string, sizeof(block_string), block_expr->identifier, "");
  
  // Generate filled 3D sphere using distance formula
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      for (int dz = -radius; dz <= radius; dz++) {
        // Check if point is within sphere using 3D distance formula
        if (dx * dx + dy * dy + dz * dz <= radius * radius) {
          int x = center_x + dx;
          int y = center_y + dy;
          int z = center_z + dz;
          
          // Check if block is within this section
          if (x >= base_x && x < base_x + 16 &&
              y >= base_y && y < base_y + 16 &&
              z >= base_z && z < base_z + 16) {
            
            // Calculate index in section
            int local_x = x - base_x;
            int local_y = y - base_y;
            int local_z = z - base_z;
            int index = local_y * 256 + local_z * 16 + local_x;
            
            // Get or create palette index for this block
            int palette_index = get_palette_index(palette, palette_size, palette_capacity, block_string);
            block_indices[index] = palette_index;
          }
        }
      }
    }
  }
}

// Array of all built-in macros
const BuiltinMacro BUILTIN_MACROS[] = {
  {"sphere", builtin_macro_sphere},
  // Add more built-in macros here in the future
};

const int BUILTIN_MACROS_COUNT = sizeof(BUILTIN_MACROS) / sizeof(BuiltinMacro);

// Helper function to register all built-in macros
void register_builtin_macros(MacroRegistry *registry) {
  for (int i = 0; i < BUILTIN_MACROS_COUNT; i++) {
    macro_registry_register(registry, BUILTIN_MACROS[i].name, BUILTIN_MACROS[i].generator);
  }
}
