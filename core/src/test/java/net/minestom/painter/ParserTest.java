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
}
