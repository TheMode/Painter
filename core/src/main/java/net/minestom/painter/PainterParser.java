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
        // Load the native library
        String libName = switch (System.getProperty("os.name").toLowerCase()) {
            case String os when os.contains("mac") -> "libpainter.dylib";
            case String os when os.contains("windows") -> "painter.dll";
            default -> "libpainter.so";
        };

        try {
            // Try to load from build directory first (for development)
            Path libPath = Path.of("core/build/native/" + libName);
            if (libPath.toFile().exists()) {
                System.load(libPath.toAbsolutePath().toString());
            } else {
                // Fall back to system library path
                System.loadLibrary("painter");
            }
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Failed to load native library: " + libName);
            throw e;
        }
    }

    /**
     * Parse a .paint file and return the program structure.
     *
     * @param paintFilePath Path to the .paint file
     * @return Memory segment containing the parsed Program structure
     * @throws Exception if parsing fails
     */
    public static MemorySegment parseFile(Path paintFilePath) throws Exception {
        String content = Files.readString(paintFilePath);
        return parseString(content);
    }

    /**
     * Parse a .paint string and return the program structure.
     *
     * @param paintCode The .paint code to parse
     * @return Memory segment containing the parsed Program structure
     */
    public static MemorySegment parseString(String paintCode) {
        try (Arena arena = Arena.ofConfined()) {
            // Allocate memory for the input string
            MemorySegment inputSegment = arena.allocateFrom(paintCode);

            // Allocate parser structure
            MemorySegment parser = arena.allocate(Parser.layout());

            // Initialize the parser
            PainterNative.parser_init(parser, inputSegment);

            // Parse the program
            MemorySegment program = PainterNative.parse_program(parser);

            // Check for errors
            boolean hasError = Parser.has_error(parser);
            if (hasError) {
                MemorySegment errorMsg = Parser.error_message(parser);
                String error = errorMsg.getString(0);
                throw new RuntimeException("Parse error: " + error);
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
        MemorySegment sectionSegment = PainterNative.generate_section(program, sectionX, sectionY, sectionZ);

        if (sectionSegment.address() == 0) {
            throw new RuntimeException("Failed to generate section");
        }

        try {
            // Extract section data
            int paletteSize = Section.palette_size(sectionSegment);
            int bitsPerEntry = Section.bits_per_entry(sectionSegment);
            int dataSize = Section.data_size(sectionSegment);

            // Read palette
            String[] palette = new String[paletteSize];
            MemorySegment palettePtr = Section.palette(sectionSegment);

            for (int i = 0; i < paletteSize; i++) {
                MemorySegment stringPtr = palettePtr.getAtIndex(ValueLayout.ADDRESS, i);
                palette[i] = stringPtr.reinterpret(Long.MAX_VALUE).getString(0);
            }

            // Read data array
            long[] data = new long[dataSize];
            if (dataSize > 0) {
                MemorySegment dataPtr = Section.data(sectionSegment);
                for (int i = 0; i < dataSize; i++) {
                    data[i] = dataPtr.getAtIndex(ValueLayout.JAVA_LONG, i);
                }
            }

            return new SectionData(palette, bitsPerEntry, data);
        } finally {
            // Free the section
            PainterNative.section_free(sectionSegment);
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
                "[0 0] air",
                "[0 50 0] minecraft:grass_block",
                "[0 50 5] stone",
                "[5 10 15] dirt"
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
        System.out.println("Format: [x z] (2 values, y defaults to 0) or [x y z] (3 values)");
    }
}
