package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Macro Tests")
class MacroTest {

    @Test
    @DisplayName("Parse sphere macro syntax")
    void testParseMacro() {
        String program = "#sphere .x=8 .radius=5 .block=stone";

        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);

        // Just verify it parses without crashing
        assertTrue(count >= 0, "Should parse without error");

        PainterParser.freeProgram(programSegment);
    }

    @Test
    @DisplayName("Parse and execute sphere macro")
    void testCircleMacro() {
        String program = "#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone";

        MemorySegment programSegment = PainterParser.parseString(program);
        int count = PainterParser.getInstructionCount(programSegment);

        assertEquals(1, count, "Should parse 1 instruction");

        // Generate section at (0, 0, 0) - the sphere should be in this section
        PainterParser.SectionData section = PainterParser.generateSection(programSegment, 0, 0, 0);
        
        // Should have at least air and stone in the palette
        assertTrue(section.palette().length >= 2, "Should have at least 2 blocks in palette (air and stone)");
        
        // Verify that stone is in the palette
        boolean hasStone = false;
        for (String block : section.palette()) {
            if (block.contains("stone")) {
                hasStone = true;
                break;
            }
        }
        assertTrue(hasStone, "Palette should contain stone");

        PainterParser.freeProgram(programSegment);
    }
}
