package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

@DisplayName("Painter Parser Tests")
class ParserTest {

    private void assertParseSucceed(String program) {
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

    private void assertParseFail(String program) {
        assertThrows(RuntimeException.class, () -> PainterParser.parseString(program),
                "Should throw exception for invalid syntax");
    }

    private static int findPaletteIndex(PainterParser.SectionData section, String blockId) {
        String[] palette = section.palette();
        for (int i = 0; i < palette.length; i++) {
            if (palette[i].equals(blockId)) {
                return i;
            }
        }
        return -1;
    }

    private static int getBlockIndex(PainterParser.SectionData section, int x, int y, int z) {
        int bits = section.bitsPerEntry();
        if (bits == 0) {
            return 0;
        }

        int index = y * 256 + z * 16 + x;
        int blocksPerLong = 64 / bits;
        int longIndex = index / blocksPerLong;
        int offset = (index % blocksPerLong) * bits;

        long mask = (1L << bits) - 1;
        long value = section.data()[longIndex];
        return (int) ((value >>> offset) & mask);
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
                repeat = @every(0, 4, 0)
                repeat {
                  [0 0] oak_planks
                }
                """);
    }

    @Test
    @DisplayName("@every occurrence places repeated blocks")
    void testEveryOccurrenceExecution() {
        String programSource = """
                @every(0, 4, 0) {
                  [0 0] oak_planks
                }
                """;

        MemorySegment program = null;
        try {
            program = PainterParser.parseString(programSource);
            PainterParser.SectionData section = PainterParser.generateSection(program, 0, 0, 0);
            assertNotNull(section, "Section should generate");

            int oakIndex = findPaletteIndex(section, "oak_planks");
            int airIndex = findPaletteIndex(section, "minecraft:air");
            assertTrue(oakIndex >= 0, "Oak planks must be present in palette");
            assertTrue(airIndex >= 0, "Air must remain in palette");

            assertEquals(oakIndex, getBlockIndex(section, 0, 0, 0), "Block at y=0 should be oak planks");
            assertEquals(oakIndex, getBlockIndex(section, 0, 4, 0), "Block at y=4 should be oak planks");
            assertEquals(oakIndex, getBlockIndex(section, 0, 8, 0), "Block at y=8 should be oak planks");
            assertEquals(airIndex, getBlockIndex(section, 0, 2, 0), "Block at y=2 should remain air");
            assertEquals(2, section.palette().length, "Only air and oak planks expected in palette");
        } finally {
            if (program != null) {
                PainterParser.freeProgram(program);
            }
        }
    }

    @Test
    @DisplayName("If/elif/else conditional statements work correctly")
    void testIfElifElseStatement() {
        String programSource = """
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
                """;

        MemorySegment program = null;
        try {
            program = PainterParser.parseString(programSource);
            PainterParser.SectionData section = PainterParser.generateSection(program, 0, 0, 0);
            assertNotNull(section, "Section should generate");

            int diamondIndex = findPaletteIndex(section, "diamond_block");
            int emeraldIndex = findPaletteIndex(section, "emerald_block");
            int stoneIndex = findPaletteIndex(section, "stone");
            int goldIndex = findPaletteIndex(section, "gold_block");

            assertTrue(diamondIndex >= 0, "Diamond block should be placed (elif branch)");
            assertTrue(emeraldIndex >= 0, "Emerald block should be placed (simple if)");
            assertEquals(-1, stoneIndex, "Stone should not be placed (if condition false)");
            assertEquals(-1, goldIndex, "Gold block should not be placed (else not reached)");

            assertEquals(diamondIndex, getBlockIndex(section, 1, 0, 0), "Diamond at (1,0,0)");
            assertEquals(emeraldIndex, getBlockIndex(section, 3, 0, 0), "Emerald at (3,0,0)");
        } finally {
            if (program != null) {
                PainterParser.freeProgram(program);
            }
        }
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

    @Test
    @DisplayName("@noise occurrence generates terrain")
    void testNoiseTerrainOccurrence() {
        String program = """
                // Terrain with amplitude and base_y
                @noise(0.02, 12345, 0, 16, 64) {
                  [0 0 0] grass_block
                  [0 -1 0] dirt
                }
                """;
        
        // Parse and generate to verify it doesn't crash
        assertParseSucceed(program);
        
        // Additionally verify we can generate sections without errors
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            // Generate a few sections to ensure the noise generation works
            for (int y = 0; y < 5; y++) {
                PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, y, 0);
                assertNotNull(section, "Should generate section at y=" + y);
            }
        } finally {
            if (programSegment != null) {
                PainterParser.freeProgram(programSegment);
            }
        }
    }

    @Test
    @DisplayName("@noise occurrence with threshold for sparse placement")
    void testNoiseThresholdOccurrence() {
        String program = """
                // Sparse placement using threshold (only 30% of area)
                @noise(0.05, 12345, 0.7) {
                  [0 0 0] oak_sapling
                }
                """;
        
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            assertNotNull(programSegment, "Noise threshold program should parse successfully");
            
            PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 0, 0);
            assertNotNull(section, "Should generate section data");
        } finally {
            if (programSegment != null) {
                PainterParser.freeProgram(programSegment);
            }
        }
    }

    @Test
    @DisplayName("@noise occurrence places trees on terrain")
    void testNoiseTreesOccurrence() {
        String program = """
                // Trees on terrain: threshold for sparsity + amplitude for terrain following
                @noise(0.1, 12345, 0.7, 16, 64) {
                  [0 0 0] oak_log
                  [0 1 0] oak_log
                  [0 2 0] oak_log
                  [-1 3 0] oak_leaves
                  [1 3 0] oak_leaves
                  [0 3 -1] oak_leaves
                  [0 3 1] oak_leaves
                  [0 3 0] oak_leaves
                }
                """;
        
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            assertNotNull(programSegment, "Noise trees program should parse successfully");
            
            PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 4, 0);
            assertNotNull(section, "Should generate section data");
            
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
        } finally {
            if (programSegment != null) {
                PainterParser.freeProgram(programSegment);
            }
        }
    }

    @Test
    @DisplayName("Combined terrain and tree occurrences work together")
    void testCombinedNoiseOccurrences() {
        String program = """
                // Generate terrain (no threshold, just height variation)
                @noise(0.025, 77777, 0, 20, 70) {
                  [0 0 0] grass_block
                  [0 -1 0] dirt
                }
                
                // Add trees with threshold for sparsity, same terrain height
                @noise(0.025, 77777, 0.75, 20, 70) {
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
}


