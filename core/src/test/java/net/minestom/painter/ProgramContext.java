package net.minestom.painter;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertNotNull;

/**
 * Context object provided to tests annotated with @PaintTest.
 * Contains the parsed program and utility methods for working with it.
 */
public record ProgramContext(MemorySegment program) {
    
    /**
     * Generate a section from the program at the specified coordinates.
     * Automatically asserts that the section is not null.
     */
    public PainterParser.SectionData generateSection(int sectionX, int sectionY, int sectionZ) {
        PainterParser.SectionData section = PainterParser.generateSection(program, sectionX, sectionY, sectionZ);
        assertNotNull(section, "Section should generate successfully");
        return section;
    }
}
