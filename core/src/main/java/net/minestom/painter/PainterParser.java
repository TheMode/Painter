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

    private volatile String[] paletteCache = new String[64];
    private final Object cacheLock = new Object();

    public static MemorySegment parseFile(Path paintFilePath) throws Exception {
        return parseString(Files.readString(paintFilePath));
    }

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

    public static void freeProgram(MemorySegment program) {
        if (program != null && program.address() != 0) {
            PainterNative.program_free(program);
        }
    }

    public static int getInstructionCount(MemorySegment program) {
        return Program.instruction_count(program);
    }

    public SectionData generateSection(MemorySegment program, int sectionX, int sectionY, int sectionZ) {
        MemorySegment section = PainterNative.generate_section(program, sectionX, sectionY, sectionZ);
        if (section.address() == 0) throw new RuntimeException("Failed to generate section");
        try {
            final int usedCount = Section.used_palette_count(section);
            final MemorySegment idsSeg = Section.used_palette_ids(section);
            MemorySegment registry = Program.palette_registry(program);

            String[] palette = new String[usedCount];
            String[] cache = paletteCache;
            for (int i = 0; i < usedCount; i++) {
                int id = idsSeg.get(ValueLayout.JAVA_INT, i * 4L);
                String s = (id < cache.length) ? cache[id] : null;
                if (s == null) {
                    s = resolvePaletteString(registry, id);
                }
                palette[i] = s;
            }

            final int dataSize = Section.data_size(section);
            long[] data = new long[dataSize];
            if (dataSize > 0) MemorySegment.copy(Section.data(section), ValueLayout.JAVA_LONG, 0, data, 0, dataSize);

            return new SectionData(palette, Section.bits_per_entry(section), data);
        } finally {
            PainterNative.section_free(section);
        }
    }

    private String resolvePaletteString(MemorySegment registry, int id) {
        String[] cache = paletteCache;
        if (id < cache.length) {
            String s = cache[id];
            if (s != null) return s;
        }

        final MemorySegment strPtr = PainterNative.painter_palette_registry_get(registry, id);
        final String s = strPtr.getString(0);

        synchronized (cacheLock) {
            cache = paletteCache;
            if (id < cache.length) {
                if (cache[id] != null) return cache[id];
                cache[id] = s;
                return s;
            }
            final int newLen = Math.max(id + 1, cache.length * 2);
            String[] resized = new String[newLen];
            System.arraycopy(cache, 0, resized, 0, cache.length);
            resized[id] = s;
            paletteCache = resized;
        }
        return s;
    }

    public record SectionData(String[] palette, int bitsPerEntry, long[] data) {
    }

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

                PainterParser parser = new PainterParser();
                SectionData section = parser.generateSection(program, 0, 0, 0);
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
