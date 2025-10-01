package net.minestom.paint;

import net.minestom.server.coordinate.Pos;
import net.minestom.server.entity.Player;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.jetbrains.annotations.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import static net.minestom.server.coordinate.CoordConversion.*;

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
     *
     * @param instance        The instance to reload
     * @param waitingInstance The instance to move players to during reload (can be null if no players)
     * @param programCode     The new painter program code
     * @param source          Description of where the code came from
     * @throws RuntimeException if the new program fails to compile
     */
    public static void reload(InstanceContainer instance, @Nullable InstanceContainer waitingInstance, 
                            String programCode, String source) {
        LOGGER.info("Reloading generator from: {}", source);

        // Compile the new generator
        PaintGenerator newGenerator;
        try {
            newGenerator = PaintGenerator.load(programCode);
        } catch (Exception e) {
            LOGGER.error("Failed to compile new generator from: {}", source, e);
            throw new RuntimeException("Failed to compile painter program: " + e.getMessage(), e);
        }

        // Save player positions and move them to waiting instance
        Map<Player, Pos> playerPositions = new HashMap<>();
        if (waitingInstance != null) {
            for (Player player : instance.getPlayers()) {
                playerPositions.put(player, player.getPosition());
                player.setInstance(waitingInstance, new Pos(0, 65, 0)).join();
            }
            LOGGER.info("Moved {} players to waiting instance", playerPositions.size());
        }

        // Get all currently loaded chunks before changing the generator
        Set<Long> loadedChunks = ConcurrentHashMap.newKeySet();
        instance.getChunks().forEach(chunk -> loadedChunks.add(chunkIndex(chunk.getChunkX(), chunk.getChunkZ())));

        LOGGER.info("Found {} loaded chunks to regenerate", loadedChunks.size());

        // Set the new generator
        instance.setGenerator(newGenerator);

        // Regenerate all loaded chunks
        for (long chunkKey : loadedChunks) {
            final int chunkX = chunkIndexGetX(chunkKey);
            final int chunkZ = chunkIndexGetZ(chunkKey);

            // Unload and reload to regenerate with new generator
            instance.unloadChunk(chunkX, chunkZ);
            instance.loadChunk(chunkX, chunkZ);
        }

        LOGGER.info("Successfully regenerated {} chunks with new generator", loadedChunks.size());

        // Move players back to their original positions
        for (Map.Entry<Player, Pos> entry : playerPositions.entrySet()) {
            Player player = entry.getKey();
            Pos originalPos = entry.getValue();
            player.setInstance(instance, originalPos).join();
        }
        
        if (!playerPositions.isEmpty()) {
            LOGGER.info("Returned {} players to regenerated world", playerPositions.size());
        }

        LOGGER.info("Successfully reloaded generator from: {}", source);
    }
}
