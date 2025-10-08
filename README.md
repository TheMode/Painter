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
  - `@noise2d` - 2D noise-based placement (terrain, trees, surface features)
  - `@noise3d` - 3D noise-based placement (caves, volumetric features)

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
  [0, 0, 0] oak_planks
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
  [0, 0, 0] oak_log
  [0, 1, 0] oak_log
  [0, 2, 0] oak_log
  [-1, 3, 0] oak_leaves
  [0, 3, 0] oak_leaves
  [1, 3, 0] oak_leaves
}

// Place a beacon at specific section coordinates
@section .x=5 .y=10 .z=7 {
  [0, 0, 0] beacon
}

// Place at section origin (0, 0, 0)
@section {
  [0, 0, 0] bedrock
}
```

**Use Cases:**
- Section markers/landmarks
- Regular structure placement
- Chunk-aligned features
- Debug visualization

### Noise2D Occurrence

2D noise sampling over the XZ plane, ideal for heightfields, surface decals, trees, and ores. Uses FastNoiseLite OpenSimplex2 under the hood.

```painter
@noise2d .frequency=0.02 .seed=12345 [.threshold=<0-1>] [.spread=<blocks>] [.y=<base_y>] {
  // Blocks to place
}
```

**Parameters:**
- `.frequency` - Controls pattern density/smoothness (0.01-0.2)
  - Lower values (0.01-0.03): Smooth, large-scale terrain features
  - Higher values (0.1-0.2): Noisy, small-scale patterns
- `.seed` - Random seed for reproducible patterns
- `.threshold` - Optional (0-1). Executes body only when noise >= threshold (use for sparsity)
- `.spread` - Optional. Multiplies the noise value (-1..1) and adds it to the base Y
- `.y` - Optional. Base Y level when using spread (defaults to the occurrence origin)

**Usage Patterns:**

#### Terrain Generation
Use `.spread` + `.y`, omit threshold for full coverage:
```painter
// Rolling hills varying ±16 blocks around y=64
@noise2d .frequency=0.02 .seed=12345 .spread=16 .y=64 {
  [0, 0, 0] grass_block
  [0, -1, 0] dirt
  [0, -2, 0] stone
}
```

#### Trees on Terrain
Use threshold for sparsity, spread for canopy alignment:
```painter
// Trees on ~30% of terrain, following height variations
@noise2d .frequency=0.02 .seed=12345 .threshold=0.7 .spread=16 .y=65 {
  [0, 0, 0] oak_log
  [0, 1, 0] oak_log
  [0, 2, 0] oak_log
  [0, 3, 0] oak_leaves
}
```

#### Sparse Feature Placement
Threshold only for flat placement:
```painter
// Flowers on 10% of area at y=64
@noise2d .frequency=0.1 .seed=12345 .threshold=0.9 {
  [0, 64, 0] dandelion
}
```

#### Complete Biome Example
Layer multiple `@noise2d` ranges with the same seed:

```painter
// Base terrain (full coverage)
@noise2d .frequency=0.025 .seed=77777 .spread=20 .y=64 {
  [0, 0, 0] grass_block
  [0, -1, 0] dirt
}

// Trees (25% coverage, on terrain)
@noise2d .frequency=0.025 .seed=77777 .threshold=0.75 .spread=20 .y=65 {
  [0, 0, 0] oak_log
  [0, 1, 0] oak_log
  [0, 2, 0] oak_log
  [-1, 2, 0] oak_leaves
  [1, 2, 0] oak_leaves
  [0, 3, 0] oak_leaves
}

// Flowers (5% coverage, on terrain)
@noise2d .frequency=0.025 .seed=77777 .threshold=0.95 .spread=20 .y=65 {
  [0, 0, 0] dandelion
}
```

**Pro Tip:** Reusing the same `.seed`, `.spread`, and `.y` keeps secondary features aligned with the underlying terrain.

### Noise3D Occurrence

3D noise sampling across entire sections, perfect for caves, volumetric decor, and scattered clusters.

```painter
@noise3d .frequency=0.08 .seed=424242 [.threshold=<0-1>] [.min_y=<min>] [.max_y=<max>] {
  // Blocks to place
}
```

**Parameters:**
- `.frequency` - Controls volumetric feature size
- `.seed` - Random seed for reproducible caves/features
- `.threshold` - Optional (0-1). Executes body only when noise >= threshold; omit for full coverage
- `.min_y` / `.max_y` - Optional inclusive world-height bounds to constrain sampling

**Example:**
```painter
// Glowstone clusters suspended underground
@noise3d .frequency=0.08 .seed=424242 .threshold=0.95 {
  #sphere .radius=2 .block=glowstone
}

// Iron ore between y=16 and y=48
@noise3d .frequency=0.12 .seed=11235 .threshold=0.78 .min_y=16 .max_y=48 {
  [0, 0, 0] iron_ore
  [1, 0, 0] iron_ore
  [0, 1, 0] iron_ore
  [0, 0, -1] iron_ore
}
```

## More Information

- See `worlds/tour.paint` for comprehensive language examples
- See `worlds/test_sphere.paint` for sphere macro examples
- See `worlds/noise_terrain_demo.paint` for noise terrain and tree examples
- See `worlds/advanced_terrain.paint` for complex biome generation
- See `MACRO_IMPLEMENTATION.md` for implementation details
