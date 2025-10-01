package net.minestom.paint;

import net.minestom.server.command.builder.Command;
import net.minestom.server.command.builder.arguments.ArgumentType;
import net.minestom.server.command.builder.arguments.ArgumentWord;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.CompletableFuture;

import static net.kyori.adventure.text.Component.text;
import static net.kyori.adventure.text.format.NamedTextColor.*;

/**
 * Command to load a painter program from a URL.
 * Usage: /loadpaint <url>
 * <p>
 * This enables ShaderToy-style sharing where users can share links to their world generators
 * and others can try them instantly with a single command.
 * <p>
 * This will regenerate ALL currently loaded chunks with the new generator.
 * Players will be temporarily moved to a waiting instance during regeneration.
 */
@NotNullByDefault
public final class LoadPaintCommand extends Command {
    private static final Logger LOGGER = LoggerFactory.getLogger(LoadPaintCommand.class);

    public LoadPaintCommand(InstanceContainer instance, InstanceContainer waitingInstance) {
        super("loadpaint");

        PaintUrlLoader urlLoader = new PaintUrlLoader();

        ArgumentWord urlArg = ArgumentType.Word("url");

        addSyntax((sender, context) -> {
            final String url = context.get(urlArg);
            sender.sendMessage(text("Loading painter code from: " + url, YELLOW));

            // Load asynchronously to avoid blocking the server
            CompletableFuture.runAsync(() -> {
                try {
                    final String content = urlLoader.loadFromUrl(url);

                    int chunkCount = instance.getChunks().size();
                    int playerCount = instance.getPlayers().size();
                    sender.sendMessage(text("Regenerating " + chunkCount + " loaded chunks...", GRAY));
                    if (playerCount > 0) {
                        sender.sendMessage(text("Moving " + playerCount + " players to waiting area...", GRAY));
                    }

                    // Reload the generator and regenerate all chunks
                    GeneratorReloader.reload(instance, waitingInstance, content, "URL: " + url);

                    sender.sendMessage(text("✓ World generator loaded successfully!", GREEN));
                    sender.sendMessage(text("Source: " + url, GRAY));
                    sender.sendMessage(text("✓ All " + chunkCount + " chunks regenerated!", GREEN));
                    if (playerCount > 0) {
                        sender.sendMessage(text("✓ Players returned to regenerated world!", GREEN));
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    LOGGER.error("Interrupted while loading from URL", e);
                    sender.sendMessage(text("✗ Request interrupted", RED));
                } catch (Exception e) {
                    LOGGER.error("Failed to load from URL: {}", url, e);
                    sender.sendMessage(text("✗ Failed to load: " + e.getMessage(), RED));
                }
            });
        }, urlArg);

        setDefaultExecutor((sender, context) -> {
            sender.sendMessage(text("Usage: /loadpaint <url>", RED));
            sender.sendMessage(text("Example: /loadpaint pastebin.com/ABC123", GRAY));
            sender.sendMessage(text("Supported: Pastebin, GitHub Gist, raw URLs", GRAY));
        });
    }
}
