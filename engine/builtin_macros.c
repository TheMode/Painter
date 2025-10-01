#include "builtin_macros.h"
// Sphere macro: #sphere .x=<x> .y=<y> .z=<z> .radius=<radius> .block=<block_name>
// Generates a filled 3D sphere
void builtin_macro_sphere(VariableContext *ctx, const MacroArgumentList *args,
                         FunctionRegistry *function_registry,
                         int base_x, int base_y, int base_z,
                         int *block_indices, char ***palette, 
                         int *palette_size, int *palette_capacity) {
  // Get arguments
  Expression *x_expr = macro_get_arg(args, "x");
  Expression *y_expr = macro_get_arg(args, "y");
  Expression *z_expr = macro_get_arg(args, "z");
  Expression *radius_expr = macro_get_arg(args, "radius");
  Expression *block_expr = macro_get_arg(args, "block");
  
  if (!radius_expr || !block_expr) {
    return; // Missing required arguments
  }
  
  int center_x = x_expr ? (int)painter_evaluate_expression(x_expr, ctx, function_registry) : 0;
  int center_y = y_expr ? (int)painter_evaluate_expression(y_expr, ctx, function_registry) : 0;
  int center_z = z_expr ? (int)painter_evaluate_expression(z_expr, ctx, function_registry) : 0;
  int radius = (int)painter_evaluate_expression(radius_expr, ctx, function_registry);
  
  // Get block name (must be an identifier)
  if (block_expr->type != EXPR_IDENTIFIER) {
    return; // Block must be an identifier
  }
  
  char block_string[MAX_TOKEN_VALUE_LENGTH];
  painter_format_block(block_string, sizeof(block_string), block_expr->identifier, "");
  
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
            int palette_index = painter_palette_get_or_add(palette, palette_size, palette_capacity, block_string);
            if (palette_index >= 0) {
              block_indices[index] = palette_index;
            }
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
