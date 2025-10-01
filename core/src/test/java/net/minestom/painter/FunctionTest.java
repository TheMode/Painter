package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

@DisplayName("Builtin Function Integration")
class FunctionTest {

    private static PainterParser.SectionData execute(String program) {
        MemorySegment programSegment = PainterParser.parseString(program);
        try {
            return PainterParser.generateSection(programSegment, 0, 0, 0);
        } finally {
            PainterParser.freeProgram(programSegment);
        }
    }

    private static int paletteIndex(String[] palette, String block) {
        for (int i = 0; i < palette.length; i++) {
            if (palette[i].equals(block)) {
                return i;
            }
        }
        return -1;
    }

    private static int blockAt(PainterParser.SectionData section, int x, int y, int z) {
        if (section.bitsPerEntry() == 0 || section.data().length == 0) {
            return 0;
        }

        int index = y * 256 + z * 16 + x;
        int bits = section.bitsPerEntry();
        int blocksPerLong = 64 / bits;
        int longIndex = index / blocksPerLong;
        int offset = (index % blocksPerLong) * bits;
        long mask = (1L << bits) - 1;
        long value = section.data()[longIndex];
        return (int) ((value >> offset) & mask);
    }

    @Test
    @DisplayName("min returns the smallest coordinate value")
    void minFunctionPlacesBlockAtLowestValue() {
        PainterParser.SectionData section = execute("""
                value = min(7, 3, 9)
                [value 0 0] stone
                """);

        int stoneIndex = paletteIndex(section.palette(), "stone");
        assertTrue(stoneIndex >= 0, "stone should be in the palette");
        assertEquals(stoneIndex, blockAt(section, 3, 0, 0));
        assertEquals(0, blockAt(section, 0, 0, 0));
    }

    @Test
    @DisplayName("floor collapses fractional positions")
    void floorFunctionPlacesBlockAtFlooredValue() {
        PainterParser.SectionData section = execute("""
                [floor(2.9) 0 0] stone
                """);

        int stoneIndex = paletteIndex(section.palette(), "stone");
        assertTrue(stoneIndex >= 0, "stone should be in the palette");
        assertEquals(stoneIndex, blockAt(section, 2, 0, 0));
    }

    @Test
    @DisplayName("clamp restricts coordinates within bounds")
    void clampFunctionRespectsBounds() {
        PainterParser.SectionData section = execute("""
                [clamp(32, 0, 15) 0 0] stone
                """);

        int stoneIndex = paletteIndex(section.palette(), "stone");
        assertTrue(stoneIndex >= 0, "stone should be in the palette");
        assertEquals(stoneIndex, blockAt(section, 15, 0, 0));
    }
}
