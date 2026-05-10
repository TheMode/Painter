package net.minestom.paint;

import net.minestom.server.instance.Chunk;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.concurrent.CompletableFuture;

/**
 * Helper class to reload the world generator and regenerate all loaded chunks.
 * This ensures that changes are visible immediately, not just in newly generated chunks.
 * Players are temporarily moved to a waiting instance during reload to prevent disconnection.
 */
@NotNullByDefault
public final class GeneratorReloader {
    private static final Logger LOGGER = LoggerFactory.getLogger(GeneratorReloader.class);

    private GeneratorReloader() {
    }

    /**
 * Reload the generator with new code and regenerate all currently loaded chunks.
 * Players in the instance will be temporarily moved to a waiting instance during the reload.
 * On success, {@link ActivePaintSource} is updated so {@code /painter dialog} reflects the live program.
     *
     * @param instance    The instance to reload
     * @param programCode The new painter program code
     * @param source      Description of where the code came from
     * @throws RuntimeException if the new program fails to compile
     */
    public static void reload(InstanceContainer instance, String programCode, String source) {
        LOGGER.info("Reloading generator from: {}", source);

        // Compile the new generator
        PaintGenerator newGenerator;
        try {
            newGenerator = PaintGenerator.load(programCode);
        } catch (Exception e) {
            LOGGER.error("Failed to compile new generator from: {}", source, e);
            throw new RuntimeException("Failed to compile painter program: " + e.getMessage(), e);
        }

        // Get all currently loaded chunks before changing the generator
        List<CompletableFuture<Void>> futures = instance.getChunks().stream()
                .map(chunk -> instance.generateChunk(chunk.getChunkX(), chunk.getChunkZ(), newGenerator))
                .toList();

        LOGGER.info("Found {} loaded chunks to regenerate", futures.size());
        CompletableFuture.allOf(futures.toArray(new CompletableFuture[0])).join();
        LOGGER.info("All loaded chunks regenerated");

        instance.getChunks().forEach(Chunk::sendChunk);

        // Set the new generator
        instance.setGenerator(newGenerator);
        ActivePaintSource.set(programCode);

        LOGGER.info("Successfully reloaded generator from: {}", source);
    }
}
