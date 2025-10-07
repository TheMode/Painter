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
- **Occurrences**: Pattern-based placement using noise and repetition
  - `@every` - Place blocks at regular intervals
  - `@section` - Place blocks once per section at a specific offset
  - `@noise` - General-purpose noise-based placement (terrain, trees, features)

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

## Occurrence System

Occurrences allow you to place blocks based on patterns, noise, or regular intervals. Coordinates inside occurrence bodies are relative to the occurrence anchor point.

### Every Occurrence

Place blocks at regular intervals:

```painter
@every(step_x, step_y, step_z) {
  // Blocks to place at each occurrence point
}
```

Example:
```painter
// Place a marker every 4 blocks vertically
@every(0, 4, 0) {
  [0 0 0] oak_planks
}
```

### Section Occurrence

Place blocks once per section (16x16x16) at a specific offset within the section:

```painter
@section [.x=<offset>] [.y=<offset>] [.z=<offset>] {
  // Blocks to place
}
```

**Parameters:**
- `.x` - X offset within section (0-15, default: 0)
- `.y` - Y offset within section (0-15, default: 0)  
- `.z` - Z offset within section (0-15, default: 0)

**Examples:**
```painter
// Place a tree at the center of each section (8, *, 8)
@section .x=8 .z=8 {
  [0 0 0] oak_log
  [0 1 0] oak_log
  [0 2 0] oak_log
  [-1 3 0] oak_leaves
  [0 3 0] oak_leaves
  [1 3 0] oak_leaves
}

// Place a beacon at specific section coordinates
@section .x=5 .y=10 .z=7 {
  [0 0 0] beacon
}

// Place at section origin (0, 0, 0)
@section {
  [0 0 0] bedrock
}
```

**Use Cases:**
- Section markers/landmarks
- Regular structure placement
- Chunk-aligned features
- Debug visualization

### Noise Occurrence

General-purpose noise-based placement using FastNoiseLite. This single occurrence type can handle terrain generation, tree placement, and sparse feature distribution.

```painter
@noise(frequency, seed, [threshold], [amplitude], [base_y], [dimensions]) {
  // Blocks to place
}
```

**Parameters:**
- `frequency` - Controls pattern density/smoothness (0.01-0.2)
  - Lower values (0.01-0.03): Smooth, large-scale terrain features
  - Higher values (0.1-0.2): Noisy, small-scale patterns
- `seed` - Random seed for reproducible patterns
- `threshold` - Optional (0-1). If provided, body only executes where noise > threshold
  - Use to control sparsity: 0.7 = 30% coverage, 0.9 = 10% coverage
- `amplitude` - Optional. Multiplies noise and adds to Y coordinate for terrain height
- `base_y` - Optional. Base Y level when using amplitude (default 0)
- `dimensions` - Optional. Defaults to 2; set to 3 to sample full 3D volume

**Usage Patterns:**

#### Terrain Generation
Use amplitude + base_y, omit or set threshold to 0:
```painter
// Rolling hills varying ±16 blocks around y=64
@noise(0.02, 12345, 0, 16, 64) {
  [0 0 0] grass_block
  [0 -1 0] dirt
  [0 -2 0] stone
}
```

#### Trees on Terrain
Use threshold for sparsity + amplitude to follow terrain:
```painter
// Trees on 30% of terrain, following height variations
@noise(0.02, 12345, 0.7, 16, 65) {
  // base_y=65 (terrain+1) places tree on top
  [0 0 0] oak_log
  [0 1 0] oak_log
  [0 2 0] oak_log
  [0 3 0] oak_leaves
}
```

#### Sparse Feature Placement
Use threshold only (no amplitude) for flat sparse placement:
```painter
// Flowers on 10% of area at y=64
@noise(0.1, 12345, 0.9) {
  [0 64 0] dandelion
}
```

#### Volumetric Scattering
Sample 3D noise for floating clusters or cave features:
```painter
// Glowstone spheres scattered through a 3D volume
@noise(0.08, 424242, 0.95, 0, 0, 3) {
  #sphere .radius=2 .block=glowstone
}
```

#### Complete Biome Example
Layer multiple @noise occurrences with same seed but different thresholds:

```painter
// Base terrain (full coverage)
@noise(0.025, 77777, 0, 20, 64) {
  [0 0 0] grass_block
  [0 -1 0] dirt
}

// Trees (25% coverage, on terrain)
@noise(0.025, 77777, 0.75, 20, 65) {
  [0 0 0] oak_log
  [0 1 0] oak_log
  [0 2 0] oak_log
  [-1 2 0] oak_leaves
  [1 2 0] oak_leaves
  [0 3 0] oak_leaves
}

// Flowers (5% coverage, on terrain)
@noise(0.025, 77777, 0.95, 20, 65) {
  [0 0 0] dandelion
}
```

**Pro Tip:** Use the same seed and amplitude across occurrences to ensure features align with the terrain height!

## More Information

- See `worlds/tour.paint` for comprehensive language examples
- See `worlds/test_sphere.paint` for sphere macro examples
- See `worlds/noise_terrain_demo.paint` for noise terrain and tree examples
- See `worlds/advanced_terrain.paint` for complex biome generation
- See `MACRO_IMPLEMENTATION.md` for implementation details
