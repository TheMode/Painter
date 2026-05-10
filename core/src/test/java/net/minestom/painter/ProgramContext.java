package net.minestom.painter;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertNotNull;

public record ProgramContext(MemorySegment program, PainterParser parser) {

    public ProgramContext(MemorySegment program) {
        this(program, new PainterParser());
    }

    public PainterParser.SectionData generateSection(int sectionX, int sectionY, int sectionZ) {
        PainterParser.SectionData section = parser.generateSection(program, sectionX, sectionY, sectionZ);
        assertNotNull(section, "Section should generate successfully");
        return section;
    }
}
