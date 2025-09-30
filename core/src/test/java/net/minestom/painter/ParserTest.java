package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Basic Parsing Tests")
class ParserTest {

    @Test
    @DisplayName("Parse simple block placement")
    void testSimpleBlockPlacement() {
        String program = "[0 0] air";
        
        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);
        
        assertEquals(1, count, "Should parse 1 instruction");
        
        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Parse block with properties")
    void testBlockWithProperties() {
        String program = "[1 50 0] oak_planks[facing=north,half=top]";
        
        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);
        
        assertEquals(1, count, "Should parse 1 instruction");
        
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 3, 0);
        assertTrue(section.palette().length >= 2, "Should have blocks in palette");
        
        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Parse variable assignment")
    void testVariableAssignment() {
        String program = """
            x = 5
            [x 0] stone
            """;
        
        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);
        
        assertEquals(2, count, "Should parse 2 instructions");
        
        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Parse arithmetic expressions")
    void testArithmeticExpressions() {
        String program = """
            x = 2
            z = 3
            [x 0 z] dirt
            """;
        
        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);
        
        assertTrue(count >= 3, "Should parse all instructions");
        
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 0, 0);
        assertTrue(section.palette().length >= 2, "Should have blocks in palette");
        
        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Invalid syntax throws error")
    void testInvalidSyntax() {
        String program = "invalid syntax here";
        
        assertThrows(RuntimeException.class, () -> {
            PainterParser.parseString(program);
        }, "Should throw exception for invalid syntax");
    }
}
