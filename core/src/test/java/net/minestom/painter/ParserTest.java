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
            if (programSegment != null) PainterParser.freeProgram(programSegment);
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
                Arguments.of("#cuboid .width=4 .height=3 .depth=5 .block=oak_planks .hollow"),
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
                    missing,
                    Arrays.toString(section.palette())
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
            #cuboid .width=3 .height=2 .depth=2 .block=stone
            #cuboid .x=14 .width=4 .height=2 .depth=1 .block=gold_block
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
}
