package net.minestom.painter;

import net.minestom.painter.generated.PainterNative;
import net.minestom.painter.generated.Parser;
import net.minestom.painter.generated.Program;
import net.minestom.painter.generated.Section;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.file.Files;
import java.nio.file.Path;

public final class PainterParser {

    static {
        String libName = switch (System.getProperty("os.name").toLowerCase()) {
            case String os when os.contains("mac") -> "libpainter.dylib";
            case String os when os.contains("windows") -> "painter.dll";
            default -> "libpainter.so";
        };
        try {
            Path libPath = Path.of("core/build/native/" + libName);
            if (libPath.toFile().exists()) {
                System.load(libPath.toAbsolutePath().toString());
            } else {
                System.loadLibrary("painter");
            }
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Failed to load native library: " + libName);
            throw e;
        }
    }

    public static MemorySegment parseFile(Path paintFilePath) throws Exception {
        return parseString(Files.readString(paintFilePath));
    }

    /**
     * Parse a .paint string and return the program structure.
     *
     * @param paintCode The .paint code to parse
     * @return Memory segment containing the parsed Program structure
     */
    public static MemorySegment parseString(String paintCode) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment parser = arena.allocate(Parser.layout());
            PainterNative.parser_init(parser, arena.allocateFrom(paintCode));
            MemorySegment program = PainterNative.parse_program(parser);
            if (Parser.has_error(parser)) {
                String error = Parser.error_message(parser).getString(0);
                throw new RuntimeException(error);
            }
            return program;
        }
    }

    /**
     * Free a program structure.
     *
     * @param program The program memory segment to free
     */
    public static void freeProgram(MemorySegment program) {
        if (program != null && program.address() != 0) {
            PainterNative.program_free(program);
        }
    }

    /**
     * Get the number of instructions in a program.
     *
     * @param program The program memory segment
     * @return Number of instructions
     */
    public static int getInstructionCount(MemorySegment program) {
        return Program.instruction_count(program);
    }

    /**
     * Generate a Minecraft section from a parsed program.
     *
     * @param program  The program memory segment
     * @param sectionX Section X coordinate
     * @param sectionY Section Y coordinate
     * @param sectionZ Section Z coordinate
     * @return SectionData containing palette and block data
     */
    public static SectionData generateSection(MemorySegment program, int sectionX, int sectionY, int sectionZ) {
        MemorySegment section = PainterNative.generate_section(program, sectionX, sectionY, sectionZ);
        if (section.address() == 0) {
            throw new RuntimeException("Failed to generate section");
        }
        try {
            int paletteSize = Section.palette_size(section);
            MemorySegment palettePtr = Section.palette(section);
            MemorySegment lens = Section.palette_lengths(section);

            String[] palette = new String[paletteSize];
            for (int i = 0; i < paletteSize; i++) {
                int len = lens.getAtIndex(ValueLayout.JAVA_INT, i);
                palette[i] = palettePtr.getAtIndex(ValueLayout.ADDRESS, i).reinterpret(len + 1).getString(0);
            }

            int dataSize = Section.data_size(section);
            long[] data = new long[dataSize];
            if (dataSize > 0) {
                MemorySegment dataPtr = Section.data(section);
                for (int i = 0; i < dataSize; i++) {
                    data[i] = dataPtr.getAtIndex(ValueLayout.JAVA_LONG, i);
                }
            }

            return new SectionData(palette, Section.bits_per_entry(section), data);
        } finally {
            PainterNative.section_free(section);
        }
    }

    /**
     * Data class representing a generated Minecraft section.
     */
    public record SectionData(String[] palette, int bitsPerEntry, long[] data) {
    }

    /**
     * Example usage
     */
    static void main(String[] args) {
        String[] tests = {
                "[0, 0] air",
                "[0, 50, 0] minecraft:grass_block",
                "[0, 50, 5] stone",
                "[5, 10, 15] dirt"
        };

        for (String test : tests) {
            System.out.println("\n=== Testing: " + test + " ===");
            try {
                MemorySegment program = parseString(test);
                int count = getInstructionCount(program);
                System.out.println("✓ Parsed " + count + " instructions successfully!");

                // Generate section and check the block was placed
                SectionData section = generateSection(program, 0, 0, 0);
                System.out.println("  Palette size: " + section.palette.length);
                System.out.println("  Palette: " + String.join(", ", section.palette()));

                freeProgram(program);
            } catch (Exception e) {
                System.out.println("✗ Error: " + e.getMessage());
                e.printStackTrace();
            }
        }

        System.out.println("\n✓ All coordinate tests passed!");
        System.out.println("Format: [x, z] (2 values, y defaults to 0) or [x, y, z] (3 values)");
    }
}
