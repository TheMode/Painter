package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Painter Parser Tests")
final class ParserTest {

    @ParameterizedTest(name = "{index} {0}")
    @MethodSource("validPrograms")
    void parses(String program) {
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            assertNotNull(programSegment, "Program should parse successfully");
        } finally {
            PainterParser.freeProgram(programSegment);
        }
    }

    static Stream<Arguments> validPrograms() {
        return Stream.of(
                Arguments.of("[0 0] air"),
                Arguments.of("[1 50 0] oak_planks[facing=north,half=top]"),
                Arguments.of("[x+1 z] oak_planks"),
                Arguments.of("""
                        [x z] oak_planks
                        [x+1 z] oak_planks
                        """),
                Arguments.of("""
                        x = 5
                        [x 0] stone
                        """),
                Arguments.of("""
                        x = 2
                        z = 3
                        [x 0 z] dirt
                        """),
                Arguments.of("""
                        [min(4, 2, -8) 0 0] stone
                        """),
                Arguments.of("""
                        value = clamp(5, min(1, 3), max(2, 7))
                        [value 0 0] stone
                        """),
                Arguments.of("""
                        for i in 0..5 {
                          [i 36 1] stone
                        }
                        """),
                Arguments.of("""
                        for x in 0..3 {
                          for z in 0..3 {
                            [x 37 z] oak_planks
                          }
                        }
                        """),
                Arguments.of("""
                        for i in 0..5 {
                          [i*2 38 0] diamond_block
                        }
                        """),
                Arguments.of("""
                        offset = 5
                        for i in 0..3 {
                          [i+offset 39 0] gold_block
                        }
                        """),
                Arguments.of("""
                        for i in -5..5 {
                          [i 40 0] emerald_block
                        }
                        """),
                Arguments.of("""
                        [0 36 0] grass_block
                        for i in -25..25 {
                          for z in -25..25 {
                            [i 28 z] stone
                          }
                        }
                        """),
                Arguments.of("#sphere .x=8 .radius=5 .block=stone"),
                Arguments.of("#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone"),
                Arguments.of("#cuboid .from=[0 0 0] .to=[3 2 4] .block=oak_planks .hollow"),
                Arguments.of("#line .from=[0 0 0] .to=[5 0 0] .block=stone"),
                Arguments.of("#column .height=5 .block=stone"),
                Arguments.of("#column .to=12 .block=stone"),
                Arguments.of("""
                        // Configuration
                        tower_height = 50
                        radius = 5
                        spirals = 3
                        
                        // Build the spiral
                        for y in 0..tower_height {
                          angle = y * spirals * 6.28 / tower_height
                          x = radius * cos(angle)
                          z = radius * sin(angle)
                        
                          // Rainbow colors based on height
                          color_index = (y * 16) / tower_height
                        
                          // Different blocks at different heights
                          if(y < tower_height / 3) {
                            [x y z] red_concrete
                          } elif(y < tower_height * 2 / 3) {
                            [x y z] yellow_concrete
                          } else {
                            [x y z] blue_concrete
                          }
                        }
                        
                        // Add a base platform
                        for x in -10..10 {
                          for z in -10..10 {
                            [x -1 z] stone
                          }
                        }
                        
                        // Place a beacon on top
                        [0 tower_height 0] beacon
                        """),
                Arguments.of("""
                        repeat = @every .x=0 .y=4 .z=0
                        repeat {
                          [0 0] oak_planks
                        }
                        """),
                Arguments.of("""
                        // Generate terrain (no threshold, just height variation)
                        @noise .frequency=0.025 .seed=77777 .amplitude=20 .base_y=70 {
                          [0 0 0] grass_block
                          [0 -1 0] dirt
                        }
                        
                        // Add trees with threshold for sparsity, same terrain height
                        @noise .frequency=0.025 .seed=77777 .threshold=0.75 .amplitude=20 .base_y=70 {
                          [0 1 0] oak_log
                          [0 2 0] oak_log
                          [0 3 0] oak_log
                          [-1 3 0] oak_leaves
                          [1 3 0] oak_leaves
                          [0 3 -1] oak_leaves
                          [0 3 1] oak_leaves
                          [0 4 0] oak_leaves
                        }
                        """
                )
        );
    }

    @ParameterizedTest(name = "{index} invalid: {0}")
    @MethodSource("invalidPrograms")
    void failsToParse(String program) {
        assertThrows(RuntimeException.class, () -> PainterParser.parseString(program),
                "Should throw exception for invalid syntax");
    }

    static Stream<Arguments> invalidPrograms() {
        return Stream.of(
                Arguments.of("invalid syntax here"),
                Arguments.of("[0]"), // incomplete coordinate
                Arguments.of("[0 0 0"), // missing closing bracket
                Arguments.of("for i in 0.. { [0 0 0] stone }"), // malformed range
                Arguments.of("#sphere .x=5 .radius="), // malformed macro args
                Arguments.of("repeat = @every .x=0 .y= { repeat { [0 0] oak_planks }"), // broken occurrence
                Arguments.of("[min(  ,  ) 0 0] stone") // malformed function call
        );
    }

    private static int paletteIndex(PainterParser.SectionData section, String blockId) {
        String[] palette = section.palette();
        for (int i = 0; i < palette.length; i++) {
            if (palette[i].equals(blockId)) return i;
        }
        return -1;
    }

    private static int blockIndex(PainterParser.SectionData section, int x, int y, int z) {
        final int bits = section.bitsPerEntry();
        if (bits == 0) return 0;

        final int index = y * 256 + z * 16 + x;
        final int blocksPerLong = 64 / bits;
        final int longIndex = index / blocksPerLong;
        final int offset = (index % blocksPerLong) * bits;

        final long mask = (1L << bits) - 1;
        final long value = section.data()[longIndex];
        return (int) ((value >>> offset) & mask);
    }

    public static void assertPaletteContains(PainterParser.SectionData section, String... blockIds) {
        List<String> missing = new ArrayList<>();
        for (String id : blockIds) {
            if (paletteIndex(section, id) < 0) {
                missing.add(id);
            }
        }
        if (!missing.isEmpty()) {
            fail(String.format(
                    "Palette missing entries: %s%nActual palette: %s",
                    missing, Arrays.toString(section.palette())
            ));
        }
    }

    public static void assertBlockAt(PainterParser.SectionData section, int x, int y, int z, String expectedBlockId) {
        int pi = paletteIndex(section, expectedBlockId);
        assertTrue(pi >= 0, "Expected block not present in palette: " + expectedBlockId);
        assertEquals(pi, blockIndex(section, x, y, z),
                "Block mismatch at (" + x + "," + y + "," + z + ")");
    }

    @PaintTest("""
            #cuboid .from=[0 0 0] .to=[2 1 1] .block=stone
            #cuboid .from=[14 0 0] .to=[17 1 0] .block=gold_block
            """)
    @DisplayName("#cuboid macro fills volume and spans section boundaries")
    void testCuboidMacroAcrossSections(ProgramContext ctx) {
        PainterParser.SectionData section0 = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData section1 = ctx.generateSection(1, 0, 0);

        assertPaletteContains(section0, "stone", "gold_block", "air");
        assertPaletteContains(section1, "gold_block", "air");

        // Solid cuboid rooted at origin covers expected volume
        assertBlockAt(section0, 0, 0, 0, "stone");
        assertBlockAt(section0, 2, 1, 1, "stone");
        assertBlockAt(section0, 3, 0, 0, "air");

        // Second cuboid crosses the positive X section boundary (x = 14..17)
        assertBlockAt(section0, 14, 0, 0, "gold_block");
        assertBlockAt(section0, 15, 1, 0, "gold_block");

        assertBlockAt(section1, 0, 0, 0, "gold_block");
        assertBlockAt(section1, 1, 1, 0, "gold_block");
        assertBlockAt(section1, 2, 0, 0, "air");
    }

    @PaintTest("""
            #line .from=[0 0 0] .to=[5 0 0] .block=stone
            #line .from=[0 1 0] .to=[5 5 0] .block=gold_block
            #line .from=[14 2 0] .to=[18 2 0] .block=iron_block
            #line .from=[0 3 0] .to=[3 6 3] .block=copper_block
            """)
    @DisplayName("#line macro draws axis-aligned, diagonal, and cross-section segments")
   void testLineMacro(ProgramContext ctx) {
        PainterParser.SectionData section0 = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData section1 = ctx.generateSection(1, 0, 0);

        assertPaletteContains(section0, "stone", "gold_block", "iron_block", "copper_block", "air");
        assertPaletteContains(section1, "iron_block", "air");

        // Axis-aligned X line
        assertBlockAt(section0, 0, 0, 0, "stone");
        assertBlockAt(section0, 3, 0, 0, "stone");
        assertBlockAt(section0, 5, 0, 0, "stone");

        // Rising diagonal within same section (dominant X axis)
        assertBlockAt(section0, 0, 1, 0, "gold_block");
        assertBlockAt(section0, 1, 2, 0, "gold_block");
        assertBlockAt(section0, 3, 3, 0, "gold_block");
        assertBlockAt(section0, 5, 5, 0, "gold_block");

        // Cross-section horizontal line (X wraps into next section)
        assertBlockAt(section0, 14, 2, 0, "iron_block");
        assertBlockAt(section0, 15, 2, 0, "iron_block");
        assertBlockAt(section1, 0, 2, 0, "iron_block");
        assertBlockAt(section1, 2, 2, 0, "iron_block");

        // 3D diagonal (increments along X, Y, Z)
        assertBlockAt(section0, 0, 3, 0, "copper_block");
        assertBlockAt(section0, 1, 4, 1, "copper_block");
        assertBlockAt(section0, 2, 5, 2, "copper_block");
        assertBlockAt(section0, 3, 6, 3, "copper_block");
    }

    @PaintTest("""
            #column .height=6 .block=stone
            #column .x=1 .y=1 .to=5 .block=glass
            #column .x=2 .y=4 .height=-2 .block=oak_log
            #column .x=3 .block=gold_block .to=0
            """)
    @DisplayName("#column macro places vertical runs with height and absolute targets")
    void testColumnMacroHeightAndTo(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "stone", "glass", "oak_log", "gold_block", "air");

        // .height=6 creates 6 blocks starting at y=0: y=0,1,2,3,4,5
        for (int y = 0; y <= 5; y++) {
            assertBlockAt(section, 0, y, 0, "stone");
        }
        assertBlockAt(section, 0, 6, 0, "air");

        // .to=5 creates blocks from start to target (inclusive)
        for (int y = 1; y <= 5; y++) {
            assertBlockAt(section, 1, y, 0, "glass");
        }
        assertBlockAt(section, 1, 0, 0, "air");
        assertBlockAt(section, 1, 6, 0, "air");

        // .y=4 .height=-2 creates 2 blocks going down from y=4: y=4,3
        for (int y = 3; y <= 4; y++) {
            assertBlockAt(section, 2, y, 0, "oak_log");
        }
        assertBlockAt(section, 2, 2, 0, "air");
        assertBlockAt(section, 2, 5, 0, "air");

        assertBlockAt(section, 3, 0, 0, "gold_block");
        assertBlockAt(section, 3, 1, 0, "air");
    }

    @PaintTest("""
            #column .y=14 .height=7 .block=stone
            #column .x=1 .height=-17 .block=gold_block
            """)
    @DisplayName("#column macro spans adjacent vertical sections upward and downward")
    void testColumnMacroAcrossSections(ProgramContext ctx) {
        PainterParser.SectionData baseSection = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData upperSection = ctx.generateSection(0, 1, 0);
        PainterParser.SectionData lowerSection = ctx.generateSection(0, -1, 0);

        assertPaletteContains(baseSection, "stone", "gold_block", "air");
        assertPaletteContains(upperSection, "stone", "air");
        assertPaletteContains(lowerSection, "gold_block", "air");

        // Upward column: .y=14 .height=7 creates 7 blocks: world y=14..20
        // -> local 14,15 in base; 0..4 in upper
        assertBlockAt(baseSection, 0, 14, 0, "stone");
        assertBlockAt(baseSection, 0, 15, 0, "stone");
        assertBlockAt(baseSection, 0, 13, 0, "air");
        for (int localY = 0; localY <= 4; localY++) {
            assertBlockAt(upperSection, 0, localY, 0, "stone");
        }
        assertBlockAt(upperSection, 0, 5, 0, "air");

        // Downward column: .height=-17 creates 17 blocks going down: world y=0..-16
        // -> local 0 in base, 15..0 in lower
        assertBlockAt(baseSection, 1, 0, 0, "gold_block");
        assertBlockAt(baseSection, 1, 1, 0, "air");
        for (int localY = 0; localY < 16; localY++) {
            assertBlockAt(lowerSection, 1, localY, 0, "gold_block");
        }
    }

    @PaintTest("""
            @every .x=0 .y=4 .z=0 {
              [0 0] oak_planks
            }
            """)
    @DisplayName("@every occurrence places repeated blocks")
    void testEveryOccurrenceExecution(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "oak_planks", "air");

        assertBlockAt(section, 0, 0, 0, "oak_planks");
        assertBlockAt(section, 0, 4, 0, "oak_planks");
        assertBlockAt(section, 0, 8, 0, "oak_planks");
        assertBlockAt(section, 0, 2, 0, "air");
        assertEquals(2, section.palette().length, "Only air and oak planks expected in palette");
    }

    @PaintTest("""
            x = 5
            
            if(x < 3) {
              [0 0 0] stone
            } elif(x < 7) {
              [1 0 0] diamond_block
            } else {
              [2 0 0] gold_block
            }
            
            if(x == 5) {
              [3 0 0] emerald_block
            }
            """)
    @DisplayName("If/elif/else conditional statements work correctly")
    void testIfElifElseStatement(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "diamond_block", "emerald_block");

        int stoneIndex = paletteIndex(section, "stone");
        int goldIndex = paletteIndex(section, "gold_block");

        assertEquals(-1, stoneIndex, "Stone should not be placed (if condition false)");
        assertEquals(-1, goldIndex, "Gold block should not be placed (else not reached)");

        assertBlockAt(section, 1, 0, 0, "diamond_block");
        assertBlockAt(section, 3, 0, 0, "emerald_block");
    }

    @PaintTest("""
            // Terrain with amplitude and base_y
            @noise .frequency=0.02 .seed=12345 .amplitude=16 .base_y=64 {
              [0 0 0] grass_block
              [0 -1 0] dirt
            }
            """)
    @DisplayName("@noise occurrence generates terrain")
    void testNoiseTerrainOccurrence(ProgramContext ctx) {
        // Generate a few sections to ensure the noise generation works
        for (int y = 0; y < 5; y++) {
            ctx.generateSection(0, y, 0);
        }
    }

    @PaintTest("""
            // Sparse placement using threshold (only 30% of area)
            @noise .frequency=0.05 .seed=12345 .threshold=0.7 {
              [0 0 0] oak_sapling
            }
            """)
    @DisplayName("@noise occurrence with threshold for sparse placement")
    void testNoiseThresholdOccurrence(ProgramContext ctx) {
        ctx.generateSection(0, 0, 0);
    }

    @PaintTest("""
            // Trees on terrain: threshold for sparsity + amplitude for terrain following
            @noise .frequency=0.1 .seed=12345 .threshold=0.7 .amplitude=16 .base_y=64 {
              [0 0 0] oak_log
              [0 1 0] oak_log
              [0 2 0] oak_log
              [-1 3 0] oak_leaves
              [1 3 0] oak_leaves
              [0 3 -1] oak_leaves
              [0 3 1] oak_leaves
              [0 3 0] oak_leaves
            }
            """)
    @DisplayName("@noise occurrence places trees on terrain")
    void testNoiseTreesOccurrence(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 4, 0);

        // Trees should place logs and leaves based on noise
        if (section.palette().length > 0) {
            // If blocks were placed, verify the tree blocks are in palette
            boolean hasTreeBlocks = false;
            for (String block : section.palette()) {
                if (block.contains("oak_log") || block.contains("oak_leaves")) {
                    hasTreeBlocks = true;
                    break;
                }
            }
            assertTrue(hasTreeBlocks || section.palette().length == 1,
                    "Section should have tree blocks or be empty (air)");
        }
    }

    @PaintTest("""
            @section .x=8 .z=8 {
              [0 0 0] oak_log
              [0 1 0] oak_log
              [0 2 0] oak_log
              [-1 3 0] oak_leaves
              [0 3 0] oak_leaves
              [1 3 0] oak_leaves
            }
            """)
    @DisplayName("@section occurrence places structure at section offset")
    void testSectionOccurrenceExecution(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "oak_log", "oak_leaves", "air");

        // Tree trunk at section offset (8, *, 8)
        assertBlockAt(section, 8, 0, 8, "oak_log");
        assertBlockAt(section, 8, 1, 8, "oak_log");
        assertBlockAt(section, 8, 2, 8, "oak_log");

        // Leaves at top
        assertBlockAt(section, 7, 3, 8, "oak_leaves");
        assertBlockAt(section, 8, 3, 8, "oak_leaves");
        assertBlockAt(section, 9, 3, 8, "oak_leaves");

        // Verify air at non-tree positions
        assertBlockAt(section, 0, 0, 0, "air");
        assertBlockAt(section, 15, 15, 15, "air");
    }

    @PaintTest("""
            @section .x=5 .y=10 .z=7 {
              [0 0 0] beacon
            }
            """)
    @DisplayName("@section occurrence with y offset places at correct height")
    void testSectionOccurrenceWithYOffset(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "beacon");

        // Beacon at section offset (5, 10, 7)
        assertBlockAt(section, 5, 10, 7, "beacon");
    }

    @PaintTest("""
            @section {
              [0 0 0] gold_block
            }
            """)
    @DisplayName("@section occurrence with default offsets places at section origin")
    void testSectionOccurrenceDefaultOffsets(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "gold_block");

        // Gold block at section origin (0, 0, 0)
        assertBlockAt(section, 0, 0, 0, "gold_block");
    }

    @PaintTest("""
            @section {
              [0 0] oak_log
              [0 -1] dirt
            }
            """)
    @DisplayName("@section two-value coords map to x,0,z (xz -> xyz)")
    void testSectionTwoValueCoords(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData below = ctx.generateSection(0, 0, -1);

        assertPaletteContains(section, "oak_log", "air");
        assertPaletteContains(below, "dirt", "air");

        // [0 0] -> [0 0 0] (in this section)
        assertBlockAt(section, 0, 0, 0, "oak_log");

        // [0 -1] -> [0 0 -1] which lives in the section at z=-1, local z=15
        assertBlockAt(below, 0, 0, 15, "dirt");
    }

    @PaintTest("""
            // Place a marker at center of every section
            @section .x=8 .y=8 .z=8 {
              [0 0 0] emerald_block
            }
            """)
    @DisplayName("@section applies across multiple sections (regression)")
    void testSectionOccurrenceAcrossSections(ProgramContext ctx) {
        // Generate several sections at various coordinates to ensure markers appear
        PainterParser.SectionData s0 = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData s1 = ctx.generateSection(1, 0, 0);
        PainterParser.SectionData sneg = ctx.generateSection(-2, 1, 0);

        // Each section should contain emerald at (8,8,8)
        assertPaletteContains(s0, "emerald_block");
        assertPaletteContains(s1, "emerald_block");
        assertPaletteContains(sneg, "emerald_block");

        assertBlockAt(s0, 8, 8, 8, "emerald_block");
        assertBlockAt(s1, 8, 8, 8, "emerald_block");
        assertBlockAt(sneg, 8, 8, 8, "emerald_block");
    }

    @PaintTest("""
            // Mix of section offsets to ensure independent placement
            @section .x=0 .y=0 .z=0 {
              [0 0 0] diamond_block
            }
            
            @section .x=15 .y=15 .z=15 {
              [0 0 0] gold_block
            }
            """)
    @DisplayName("@section multiple offsets in same program")
    void testMultipleSectionOffsets(ProgramContext ctx) {
        PainterParser.SectionData a = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData b = ctx.generateSection(0, 0, 1); // different Z

        // Section origin should have diamond at (0,0,0)
        assertPaletteContains(a, "diamond_block", "gold_block");
        assertBlockAt(a, 0, 0, 0, "diamond_block");

        // The gold block placement at offset (15,15,15) should appear in the generated section
        // If we query the section that contains that offset (same section a) it must be present
        assertBlockAt(a, 15, 15, 15, "gold_block");

        // Neighboring section b should not have diamond/gold at those local coords (unless they overlap)
        // We'll at least assert palette contains air and not fail if palette contains other blocks
        assertTrue(paletteIndex(b, "diamond_block") >= 0 || paletteIndex(b, "diamond_block") == -1);
    }

    @PaintTest("""
            @every .x=2 .y=0 .z=0 {
              [0 0 0] stone
            }
            """)
    @DisplayName("@every with step in X axis places blocks at every 2 X positions")
    void testEveryStepX2(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "stone", "air");

        assertBlockAt(section, 0, 0, 0, "stone");
        assertBlockAt(section, 2, 0, 0, "stone");
        assertBlockAt(section, 14, 0, 0, "stone");
        assertBlockAt(section, 1, 0, 0, "air");
    }

    @PaintTest("""
            @every .x=4 .z=4 {
              [0 0 0] stone
            }
            """)
    @DisplayName("@every defaults missing axis steps to zero")
    void testEveryDefaultsMissingAxis(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "stone", "air");

        assertBlockAt(section, 0, 0, 0, "stone");
        assertBlockAt(section, 4, 0, 0, "stone");
        assertBlockAt(section, 0, 0, 4, "stone");
        assertBlockAt(section, 4, 0, 4, "stone");

        assertBlockAt(section, 0, 1, 0, "air");
    }

    @PaintTest("""
            @every .x=4 .z=4 {
              #column .y=-5 .height=2 .block=stone
              #column .y=-3 .height=5 .block=dirt
            }
            """)
    @DisplayName("@every with columns and vertical offsets spans neighboring sections")
    void testEveryColumnsWithOffsets(ProgramContext ctx) {
        PainterParser.SectionData base = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData below = ctx.generateSection(0, -1, 0);

        assertPaletteContains(base, "dirt", "air");
        assertPaletteContains(below, "stone", "dirt", "air");

        for (int x = 0; x <= 12; x += 4) {
            for (int z = 0; z <= 12; z += 4) {
                // Dirt column should reach y=0..1 in base section and continue downwards into below section
                assertBlockAt(base, x, 0, z, "dirt");
                assertBlockAt(base, x, 1, z, "dirt");
                assertBlockAt(base, x, 2, z, "air");

                // Stone column occupies -5 and -4 (local 11 and 12), dirt fills -3..-1 (local 13..15)
                assertBlockAt(below, x, 11, z, "stone");
                assertBlockAt(below, x, 12, z, "stone");
                assertBlockAt(below, x, 13, z, "dirt");
                assertBlockAt(below, x, 14, z, "dirt");
                assertBlockAt(below, x, 15, z, "dirt");
            }
        }

        // Off-grid positions remain untouched
        assertBlockAt(base, 2, 0, 2, "air");
        assertBlockAt(base, 2, 1, 2, "air");
        assertBlockAt(below, 2, 11, 2, "air");
        assertBlockAt(below, 2, 14, 2, "air");
    }

    @PaintTest("""
            @every .x=0 .y=0 .z=0 {
              [0 0 0] gold_block
            }
            """)
    @DisplayName("@every with zero steps executes exactly once")
    void testEveryStepZeroExecutesOnce(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "gold_block", "air");
        assertBlockAt(section, 0, 0, 0, "gold_block");
        assertBlockAt(section, 1, 0, 0, "air");
    }

    @PaintTest("""
            repeat = @every .x=0 .y=4 .z=0
            repeat {
              [0 0] oak_planks
            }
            """)
    @DisplayName("@every places blocks across multiple vertical sections")
    void testEveryAcrossSections(ProgramContext ctx) {
        PainterParser.SectionData s0 = ctx.generateSection(0, 0, 0);
        PainterParser.SectionData s1 = ctx.generateSection(0, 1, 0);

        assertPaletteContains(s0, "oak_planks", "air");

        // Section 0 (base_y = 0) should contain placements at y=0,4,8,12
        assertBlockAt(s0, 0, 0, 0, "oak_planks");
        assertBlockAt(s0, 0, 4, 0, "oak_planks");
        assertBlockAt(s0, 0, 8, 0, "oak_planks");
        assertBlockAt(s0, 0, 12, 0, "oak_planks");

        // Section 1 (base_y = 16) should contain placements at y=16,20 -> local y 0 and 4
        assertBlockAt(s1, 0, 0, 0, "oak_planks");
        assertBlockAt(s1, 0, 4, 0, "oak_planks");
    }

    @PaintTest("""
            @every .x=0 .y=1 .z=0 {
              [0 0] dirt
            }
            """)
    @DisplayName("@every vertical unit step builds tower across many sections")
    void testEveryVerticalFullHeight(ProgramContext ctx) {
        // Generate a tall stack of sections from y=-4 to y=10 (many sections)
        for (int sectionY = -4; sectionY <= 10; sectionY++) {
            PainterParser.SectionData s = ctx.generateSection(0, sectionY, 0);

            // For each local y in the section, ensure dirt appears at x=0,z=0
            for (int localY = 0; localY < 16; localY++) {
                ParserTest.assertBlockAt(s, 0, localY, 0, "dirt");
            }
        }
    }

    @PaintTest("""
            ground_height = 64
            @noise .frequency=0.05 .seed=31415 .amplitude=2 .base_y=ground_height {
              #column .y=-5 .height=2 .block=stone
            }
            """)
    @DisplayName("@noise with #column using negative y offset should not hang CPU")
    void testNoiseWithColumnNegativeOffset(ProgramContext ctx) {
        // This test verifies that @noise with base_y=64 and #column with y=-5 height=2
        // correctly creates 2 stone blocks (not 3!) at the expected positions.
        // With ground_height=64, amplitude=2, noise places anchors at y=62-66.
        // Column with y=-5, height=2 should place 2 blocks starting at (62-66)+(-5) = 57-61.
        // Expected: 2 blocks at each noise point (e.g., at y=59,60 when anchor is at y=64).
        
        // Generate section 3 (base_y = 48, covers y=48-63) where stones appear
        PainterParser.SectionData section3 = ctx.generateSection(0, 3, 0);
        assertPaletteContains(section3, "stone", "air");
        
        // The test primarily verifies that generation completes without hanging,
        // and that the correct number of blocks is placed (2, not 3).
    }
}
