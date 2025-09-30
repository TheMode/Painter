package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

@DisplayName("For Loop Tests")
class TestForLoops {

    @Test
    @DisplayName("Simple for loop places blocks correctly")
    void testSimpleForLoop() {
        String program = """
                for i in 0..5 {
                  [i 36 1] stone
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 2, 0);

        // Should have air and stone in palette
        assertEquals(2, section.palette().length, "Should have 2 blocks in palette");
        assertTrue(containsBlock(section.palette(), "minecraft:air"), "Should contain air");
        assertTrue(containsBlock(section.palette(), "stone"), "Should contain stone");
    }

    @Test
    @DisplayName("Nested loops create grid pattern")
    void testNestedLoops() {
        String program = """
                for x in 0..3 {
                  for z in 0..3 {
                    [x 37 z] oak_planks
                  }
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 2, 0);

        // Should have air and oak_planks in palette
        assertEquals(2, section.palette().length, "Should have 2 blocks in palette");
        assertTrue(containsBlock(section.palette(), "oak_planks"), "Should contain oak_planks");

        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Loop with arithmetic expressions")
    void testLoopWithArithmetic() {
        String program = """
                for i in 0..5 {
                  [i*2 38 0] diamond_block
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 2, 0);

        assertTrue(containsBlock(section.palette(), "diamond_block"), "Should contain diamond_block");

        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Loop with variables")
    void testLoopWithVariables() {
        String program = """
                offset = 5
                for i in 0..3 {
                  [i+offset 39 0] gold_block
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 2, 0);

        assertTrue(containsBlock(section.palette(), "gold_block"), "Should contain gold_block");

        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Loop with negative range")
    void testLoopWithNegativeRange() {
        String program = """
                for i in -5..5 {
                  [i 40 0] emerald_block
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 2, 0);

        assertTrue(containsBlock(section.palette(), "emerald_block"), "Should contain emerald_block");

        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Nested loops with negative ranges")
    void testNestedLoopsWithNegativeRanges() {
        String program = """
                [0 36 0] minecraft:grass_block
                for i in -25..25 {
                  for z in -25..25 {
                    [i 28 z] stone
                  }
                }
                """;

        MemorySegment programSegment = PainterParser.parseString(program);
        assertEquals(2, PainterParser.getInstructionCount(programSegment), "Should have 2 instructions");

        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 1, 0);
        assertTrue(containsBlock(section.palette(), "stone"), "Should contain stone");

        PainterParser.freeProgram(programSegment);
    }

    private boolean containsBlock(String[] palette, String blockName) {
        for (String block : palette) {
            if (block.equals(blockName) || block.equals("minecraft:" + blockName)) {
                return true;
            }
        }
        return false;
    }
}
