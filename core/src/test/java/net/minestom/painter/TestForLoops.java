package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("For Loop Tests")
class ForLoopTest {

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

    private boolean containsBlock(String[] palette, String blockName) {
        for (String block : palette) {
            if (block.equals(blockName) || block.equals("minecraft:" + blockName)) {
                return true;
            }
        }
        return false;
    }
}
