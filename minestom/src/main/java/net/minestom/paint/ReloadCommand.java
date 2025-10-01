package net.minestom.paint;

import net.minestom.server.command.builder.Command;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static net.kyori.adventure.text.Component.text;
import static net.kyori.adventure.text.format.NamedTextColor.*;

/**
 * Command to reload the world generator from the original file.
 * Usage: /reload
 * <p>
 * This will regenerate ALL currently loaded chunks with the new generator.
 * Players will be temporarily moved to a waiting instance during regeneration.
 */
@NotNullByDefault
public final class ReloadCommand extends Command {
    private static final Logger LOGGER = LoggerFactory.getLogger(ReloadCommand.class);

    public ReloadCommand(InstanceContainer instance, InstanceContainer waitingInstance, Path paintFile) {
        super("reload");

        setDefaultExecutor((sender, context) -> {
            sender.sendMessage(text("Reloading world generator from file...", YELLOW));

            try {
                final String content = Files.readString(paintFile);

                final int chunkCount = instance.getChunks().size();
                final int playerCount = instance.getPlayers().size();
                sender.sendMessage(text("Regenerating " + chunkCount + " loaded chunks...", GRAY));
                if (playerCount > 0) {
                    sender.sendMessage(text("Moving " + playerCount + " players to waiting area...", GRAY));
                }

                GeneratorReloader.reload(instance, waitingInstance, content, paintFile.toString());

                sender.sendMessage(text("✓ World generator reloaded successfully!", GREEN));
                sender.sendMessage(text("✓ All " + chunkCount + " chunks regenerated!", GREEN));
                if (playerCount > 0) {
                    sender.sendMessage(text("✓ Players returned to regenerated world!", GREEN));
                }
            } catch (IOException e) {
                LOGGER.error("Failed to read paint file", e);
                sender.sendMessage(text("✗ Failed to read file: " + e.getMessage(), RED));
            } catch (Exception e) {
                LOGGER.error("Failed to reload generator", e);
                sender.sendMessage(text("✗ Failed to reload: " + e.getMessage(), RED));
            }
        });
    }
}
