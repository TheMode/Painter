package net.minestom.paint;

import net.minestom.painter.PainterParser;
import net.minestom.server.coordinate.Vec;
import net.minestom.server.instance.block.Block;
import net.minestom.server.instance.generator.GenerationUnit;
import net.minestom.server.instance.generator.Generator;
import net.minestom.server.instance.palette.Palette;
import org.jetbrains.annotations.NotNullByDefault;
import org.jetbrains.annotations.Nullable;

import java.lang.foreign.MemorySegment;

import static net.minestom.server.coordinate.CoordConversion.SECTION_SIZE;

@NotNullByDefault
public final class PaintGenerator implements Generator {
    private final PainterParser parser = new PainterParser();
    private final MemorySegment program;

    public static PaintGenerator load(String programCode) {
        MemorySegment program = PainterParser.parseString(programCode);
        return new PaintGenerator(program);
    }

    private PaintGenerator(MemorySegment program) {
        this.program = program;
    }

    @Override
    public void generate(GenerationUnit unit) {
        for (Vec section : unit.sections()) {
            final int sectionX = section.blockX(), sectionY = section.blockY(), sectionZ = section.blockZ();

            // Generate section data from the painter program
            PainterParser.SectionData sectionData = parser.generateSection(
                    program, sectionX, sectionY, sectionZ
            );

            Palette blocks = generatePalette(sectionData);
            if (blocks == null) continue;
            blocks.getAllPresent((x, y, z, value) -> {
                final Block block = Block.fromStateId(value);
                assert block != null;
                final int globalX = x + sectionX * SECTION_SIZE;
                final int globalY = y + sectionY * SECTION_SIZE;
                final int globalZ = z + sectionZ * SECTION_SIZE;
                unit.modifier().setBlock(globalX, globalY, globalZ, block);
            });
        }
    }

    private static @Nullable Palette generatePalette(PainterParser.SectionData sectionData) {
        // Get palette and data array
        final String[] palette = sectionData.palette();
        final long[] data = sectionData.data();
        final int bitsPerEntry = sectionData.bitsPerEntry();
        if (data.length == 0) {
            // Empty section, skip
            return null;
        }

        // Convert to block state ids
        int[] blockStateIds = new int[palette.length];
        for (int i = 0; i < palette.length; i++) {
            Block block = Block.fromState(palette[i]);
            if (block == null) continue; // Invalid block state
            blockStateIds[i] = block.stateId();
        }

        Palette blocks = Palette.blocks(bitsPerEntry);
        blocks.load(blockStateIds, data);
        return blocks;
    }

    public void close() {
        PainterParser.freeProgram(program);
    }
}
