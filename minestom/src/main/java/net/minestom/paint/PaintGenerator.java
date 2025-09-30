package net.minestom.paint;

import net.minestom.painter.PainterParser;
import net.minestom.server.coordinate.Vec;
import net.minestom.server.instance.block.Block;
import net.minestom.server.instance.generator.GenerationUnit;
import net.minestom.server.instance.generator.Generator;
import net.minestom.server.instance.palette.Palettes;
import org.jetbrains.annotations.NotNullByDefault;

import java.lang.foreign.MemorySegment;

import static net.minestom.server.coordinate.CoordConversion.SECTION_SIZE;

@NotNullByDefault
public final class PaintGenerator implements Generator {
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
            final int sectionX = section.blockX();
            final int sectionY = section.blockY();
            final int sectionZ = section.blockZ();

            // Generate section data from the painter program
            PainterParser.SectionData sectionData = PainterParser.generateSection(
                    program, sectionX, sectionY, sectionZ
            );

            // Get palette and data array
            final String[] palette = sectionData.palette();
            final long[] data = sectionData.data();
            final int bitsPerEntry = sectionData.bitsPerEntry();

            if (data.length == 0) {
                // Empty section, skip
                continue;
            }

            // Convert to block state ids
            int[] blockStateIds = new int[palette.length];
            for (int i = 0; i < palette.length; i++) {
                Block block = Block.fromState(palette[i]);
                assert block != null;
                blockStateIds[i] = block.stateId();
            }

            for (int x = 0; x < 16; x++) {
                for (int y = 0; y < 16; y++) {
                    for (int z = 0; z < 16; z++) {
                        int value = Palettes.read(16, bitsPerEntry, data, x, y, z);
                        final int index = blockStateIds[value];
                        Block block = Block.fromStateId(index);
                        assert block != null;

                        final int globalX = x + sectionX * SECTION_SIZE;
                        final int globalY = y + sectionY * SECTION_SIZE;
                        final int globalZ = z + sectionZ * SECTION_SIZE;
                        unit.modifier().setBlock(globalX, globalY, globalZ, block);
                    }
                }
            }
        }
    }

    public void close() {
        PainterParser.freeProgram(program);
    }
}
