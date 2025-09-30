# Paint world format

A declarative language for generating Minecraft worlds programmatically.

## Features

- **Block Placement**: Place blocks with coordinates and properties
- **Variables**: Define and use variables in expressions
- **Loops**: Generate patterns with `for` loops
- **Arithmetic**: Full expression support with +, -, *, /, %
- **Macros**: Reusable generators for complex patterns (NEW!)
  - `#sphere` - Generate filled 3D spheres
  - Easy to add custom macros

## Macro System

### Sphere Macro

Generate filled 3D spheres:

```painter
#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone
```

Parameters:
- `.x` - X coordinate of sphere center (required)
- `.y` - Y coordinate of sphere center (optional, defaults to 0)
- `.z` - Z coordinate of sphere center (optional, defaults to 0)  
- `.radius` - Sphere radius (required)
- `.block` - Block type (required)

All parameters support variables and expressions!

### Examples

```painter
// Sphere at ground level
#sphere .x=8 .y=0 .radius=5 .block=stone

// Sphere floating in the air
#sphere .x=8 .y=20 .z=8 .radius=5 .block=stone

// Using variables
radius = 7
center_y = 10
#sphere .x=8 .y=center_y .radius=radius .block=dirt

// Multiple spheres
#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone
#sphere .x=20 .y=5 .z=8 .radius=3 .block=oak_planks
```

See `MACRO_IMPLEMENTATION.md` for details on adding custom macros.

## More Information

- See `worlds/tour.paint` for comprehensive language examples
- See `worlds/test_sphere.paint` for sphere macro examples
- See `MACRO_IMPLEMENTATION.md` for implementation details