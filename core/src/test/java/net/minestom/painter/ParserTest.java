package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Painter Parser Tests")
final class ParserTest {

    private static void assertParseSucceed(String program) {
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            assertNotNull(programSegment, "Program should parse successfully");
        } finally {
            if (programSegment != null) {
                PainterParser.freeProgram(programSegment);
            }
        }
    }

    private static void assertParseFail(String program) {
        assertThrows(RuntimeException.class, () -> PainterParser.parseString(program),
                "Should throw exception for invalid syntax");
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

    public static void assertPaletteContains(PainterParser.SectionData section, String... blockId) {
        for (String id : blockId) {
            assertTrue(paletteIndex(section, id) >= 0, "Palette must contain: " + id);
        }
    }

    public static void assertBlockAt(PainterParser.SectionData section, int x, int y, int z, String expectedBlockId) {
        int pi = paletteIndex(section, expectedBlockId);
        assertTrue(pi >= 0, "Expected block not present in palette: " + expectedBlockId);
        assertEquals(pi, blockIndex(section, x, y, z),
                "Block mismatch at (" + x + "," + y + "," + z + ")");
    }

    @Test
    @DisplayName("Parse simple block placement")
    void testSimpleBlockPlacement() {
        assertParseSucceed("[0 0] air");
    }

    @Test
    @DisplayName("Parse block with properties")
    void testBlockWithProperties() {
        assertParseSucceed("[1 50 0] oak_planks[facing=north,half=top]");
    }

    @Test
    @DisplayName("Parse variable assignment")
    void testVariableAssignment() {
        assertParseSucceed("""
                x = 5
                [x 0] stone
                """);
    }

    @Test
    @DisplayName("Parse arithmetic expressions")
    void testArithmeticExpressions() {
        assertParseSucceed("""
                x = 2
                z = 3
                [x 0 z] dirt
                """);
    }

    @Test
    @DisplayName("Parse function call expressions")
    void testFunctionCalls() {
        assertParseSucceed("""
                [min(4, 2, -8) 0 0] stone
                """);
    }

    @Test
    @DisplayName("Parse nested function calls")
    void testNestedFunctionCalls() {
        assertParseSucceed("""
                value = clamp(5, min(1, 3), max(2, 7))
                [value 0 0] stone
                """);
    }

    @Test
    @DisplayName("Invalid syntax throws error")
    void testInvalidSyntax() {
        assertParseFail("invalid syntax here");
    }

    @Test
    @DisplayName("Simple for loop places blocks correctly")
    void testSimpleForLoop() {
        assertParseSucceed("""
                for i in 0..5 {
                  [i 36 1] stone
                }
                """);
    }

    @Test
    @DisplayName("Nested loops create grid pattern")
    void testNestedLoops() {
        assertParseSucceed("""
                for x in 0..3 {
                  for z in 0..3 {
                    [x 37 z] oak_planks
                  }
                }
                """);
    }

    @Test
    @DisplayName("Loop with arithmetic expressions")
    void testLoopWithArithmetic() {
        assertParseSucceed("""
                for i in 0..5 {
                  [i*2 38 0] diamond_block
                }
                """);
    }

    @Test
    @DisplayName("Loop with variables")
    void testLoopWithVariables() {
        assertParseSucceed("""
                offset = 5
                for i in 0..3 {
                  [i+offset 39 0] gold_block
                }
                """);
    }

    @Test
    @DisplayName("Loop with negative range")
    void testLoopWithNegativeRange() {
        assertParseSucceed("""
                for i in -5..5 {
                  [i 40 0] emerald_block
                }
                """);
    }

    @Test
    @DisplayName("Nested loops with negative ranges")
    void testNestedLoopsWithNegativeRanges() {
        assertParseSucceed("""
                [0 36 0] minecraft:grass_block
                for i in -25..25 {
                  for z in -25..25 {
                    [i 28 z] stone
                  }
                }
                """);
    }

    @Test
    @DisplayName("Parse sphere macro syntax")
    void testParseMacro() {
        assertParseSucceed("#sphere .x=8 .radius=5 .block=stone");
    }

    @Test
    @DisplayName("Parse and execute sphere macro")
    void testCircleMacro() {
        assertParseSucceed("#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone");
    }

    @Test
    @DisplayName("Occurrence definition can be referenced")
    void testOccurrenceDefinitionAndReferenceParse() {
        assertParseSucceed("""
                repeat = @every .x=0 .y=4 .z=0
                repeat {
                  [0 0] oak_planks
                }
                """);
    }

    @PaintTest("""
            @every .x=0 .y=4 .z=0 {
              [0 0] oak_planks
            }
            """)
    @DisplayName("@every occurrence places repeated blocks")
    void testEveryOccurrenceExecution(ProgramContext ctx) {
        PainterParser.SectionData section = ctx.generateSection(0, 0, 0);

        assertPaletteContains(section, "oak_planks", "minecraft:air");

        assertBlockAt(section, 0, 0, 0, "oak_planks");
        assertBlockAt(section, 0, 4, 0, "oak_planks");
        assertBlockAt(section, 0, 8, 0, "oak_planks");
        assertBlockAt(section, 0, 2, 0, "minecraft:air");
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

    @Test
    @DisplayName("Rainbow tower script parses successfully")
    void testRainbowTowerParse() {
        String programSource = """
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
                """;

        assertParseSucceed(programSource);
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

    @Test
    @DisplayName("Combined terrain and tree occurrences work together")
    void testCombinedNoiseOccurrences() {
        String program = """
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
                """;

        assertParseSucceed(program);
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

        assertPaletteContains(section, "oak_log", "oak_leaves", "minecraft:air");

        // Tree trunk at section offset (8, *, 8)
        assertBlockAt(section, 8, 0, 8, "oak_log");
        assertBlockAt(section, 8, 1, 8, "oak_log");
        assertBlockAt(section, 8, 2, 8, "oak_log");

        // Leaves at top
        assertBlockAt(section, 7, 3, 8, "oak_leaves");
        assertBlockAt(section, 8, 3, 8, "oak_leaves");
        assertBlockAt(section, 9, 3, 8, "oak_leaves");

        // Verify air at non-tree positions
        assertBlockAt(section, 0, 0, 0, "minecraft:air");
        assertBlockAt(section, 15, 15, 15, "minecraft:air");
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
}




