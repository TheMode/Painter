package net.minestom.paint;

import net.minestom.server.instance.Chunk;
import net.minestom.server.instance.IChunkLoader;
import net.minestom.server.instance.Instance;
import org.jetbrains.annotations.NotNullByDefault;
import org.jetbrains.annotations.Nullable;

@NotNullByDefault
public final class PaintLoader implements IChunkLoader {
    public static PaintLoader load(String program) {
        return new PaintLoader();
    }

    private PaintLoader() {
    }

    @Override
    public @Nullable Chunk loadChunk(Instance instance, int i, int i1) {
        return null;
    }

    @Override
    public void saveChunk(Chunk chunk) {
    }
}
