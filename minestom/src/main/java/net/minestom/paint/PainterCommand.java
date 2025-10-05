package net.minestom.paint;

import net.minestom.server.command.builder.Command;
import net.minestom.server.command.builder.arguments.ArgumentType;
import net.minestom.server.command.builder.arguments.ArgumentWord;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.CompletableFuture;

import static net.kyori.adventure.text.Component.text;
import static net.kyori.adventure.text.format.NamedTextColor.*;

@NotNullByDefault
public final class PainterCommand extends Command {
    public PainterCommand(InstanceContainer instance, InstanceContainer waitingInstance, Path path) {
        super("painter");
        addSubcommands(
                new Load(instance, waitingInstance),
                new Reload(instance, waitingInstance, path)
        );
    }

    /**
     * Command to load a painter program from a URL.
     * Usage: /load <url>
     * <p>
     * This enables ShaderToy-style sharing where users can share links to their world generators
     * and others can try them instantly with a single command.
     * <p>
     * This will regenerate ALL currently loaded chunks with the new generator.
     * Players will be temporarily moved to a waiting instance during regeneration.
     */
    private static final class Load extends Command {
        private static final Logger LOGGER = LoggerFactory.getLogger(Load.class);

        public Load(InstanceContainer instance, InstanceContainer waitingInstance) {
            super("load");

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

    /**
     * Command to reload the world generator from the original file.
     * Usage: /reload
     * <p>
     * This will regenerate ALL currently loaded chunks with the new generator.
     * Players will be temporarily moved to a waiting instance during regeneration.
     */
    public static final class Reload extends Command {
        private static final Logger LOGGER = LoggerFactory.getLogger(Reload.class);

        public Reload(InstanceContainer instance, InstanceContainer waitingInstance, Path paintFile) {
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
}
