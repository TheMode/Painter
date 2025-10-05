package net.minestom.paint;

import net.minestom.server.MinecraftServer;
import net.minestom.server.adventure.audience.Audiences;
import net.minestom.server.command.CommandManager;
import net.minestom.server.coordinate.Pos;
import net.minestom.server.entity.GameMode;
import net.minestom.server.entity.Player;
import net.minestom.server.event.GlobalEventHandler;
import net.minestom.server.event.player.AsyncPlayerConfigurationEvent;
import net.minestom.server.event.player.PlayerSpawnEvent;
import net.minestom.server.instance.InstanceContainer;
import net.minestom.server.instance.InstanceManager;
import net.minestom.server.instance.LightingChunk;
import net.minestom.server.instance.block.Block;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.swing.*;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static net.kyori.adventure.text.Component.text;
import static net.kyori.adventure.text.format.NamedTextColor.*;

public final class Demo {
    private static final Logger LOGGER = LoggerFactory.getLogger(Demo.class);
    private static final boolean ENABLE_FILE_WATCHER = Boolean.parseBoolean(
            System.getProperty("painter.enableFileWatcher", "true")
    );
    private static final boolean ENABLE_LOAD_COMMANDS = Boolean.parseBoolean(
            System.getProperty("painter.enableLoadCommands", "true")
    );

    static void main(String[] args) throws IOException {
        MinecraftServer minecraftServer = MinecraftServer.init();

        // Create the instance
        InstanceManager instanceManager = MinecraftServer.getInstanceManager();
        InstanceContainer instance = instanceManager.createInstanceContainer();
        instance.setChunkSupplier(LightingChunk::new);

        // Create a waiting instance for safe player teleportation during reloads
        InstanceContainer waitingInstance = instanceManager.createInstanceContainer();
        waitingInstance.setChunkSupplier(LightingChunk::new);
        // Preload a simple waiting area
        waitingInstance.setGenerator(unit -> unit.modifier().fillHeight(0, 63, Block.BEDROCK));
        waitingInstance.loadChunk(0, 0).join();
        LOGGER.info("Created waiting instance for reload safety");

        // Load the initial painter program
        Path paintFile = Path.of("worlds", "tour.paint");
        final String program = Files.readString(paintFile);

        // Load the initial generator
        PaintGenerator generator = PaintGenerator.load(program);
        instance.setGenerator(generator);
        instance.loadChunk(0, 0).join();

        LOGGER.info("Initial world generator loaded from: {}", paintFile);

        // Set up file watcher for automatic live reload during development
        PaintFileWatcher fileWatcher = null;
        if (ENABLE_FILE_WATCHER) {
            try {
                fileWatcher = new PaintFileWatcher(paintFile, (content, source) -> {
                    try {
                        GeneratorReloader.reload(instance, waitingInstance, content, source);
                        LOGGER.info("Auto-reloaded generator from file change");

                        // Notify all players
                        Audiences.players().sendMessage(text("✓ World generator auto-reloaded!", GREEN));
                        Audiences.players().sendMessage(text("All loaded chunks have been regenerated!", GRAY));
                    } catch (Exception e) {
                        LOGGER.error("Failed to auto-reload generator", e);
                        Audiences.players().sendMessage(text("✗ Auto-reload failed: " + e.getMessage(), RED));
                    }
                });
                LOGGER.info("File watcher enabled - edits to {} will auto-reload", paintFile);
            } catch (IOException e) {
                LOGGER.warn("Failed to start file watcher", e);
            }
        }

        // Register commands for manual reload and URL loading
        if (ENABLE_LOAD_COMMANDS) {
            CommandManager commandManager = MinecraftServer.getCommandManager();
            commandManager.register(new PainterCommand.ReloadCommand(instance, waitingInstance, paintFile));
            commandManager.register(new PainterCommand.LoadPaintCommand(instance, waitingInstance));
            LOGGER.info("Commands registered: /reload, /loadpaint <url>");
        }

        // Add an event callback to specify the spawning instance (and the spawn position)
        GlobalEventHandler globalEventHandler = MinecraftServer.getGlobalEventHandler();
        globalEventHandler.addListener(AsyncPlayerConfigurationEvent.class, event -> {
            final Player player = event.getPlayer();
            event.setSpawningInstance(instance);
            player.setRespawnPoint(new Pos(0, 42, 0));
            player.setGameMode(GameMode.CREATIVE);
        });

        globalEventHandler.addListener(PlayerSpawnEvent.class, event -> {
            if (!event.isFirstSpawn()) return;
            Player player = event.getPlayer();
            // Welcome message with instructions
            player.sendMessage(text("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", DARK_GRAY));
            player.sendMessage(text("Welcome to Painter Demo Server!", GOLD, net.kyori.adventure.text.format.TextDecoration.BOLD));
            player.sendMessage(text(""));
            if (ENABLE_LOAD_COMMANDS) {
                player.sendMessage(text("Commands:", YELLOW));
                player.sendMessage(text("  /reload", GREEN).append(text(" - Reload from file", GRAY)));
                player.sendMessage(text("  /loadpaint <url>", GREEN).append(text(" - Load from URL", GRAY)));
                player.sendMessage(text(""));
                player.sendMessage(text("Share your creations!", AQUA));
                player.sendMessage(text("Upload your .paint file to pastebin.com or gist.github.com", GRAY));
                player.sendMessage(text("Then share the link for others to try!", GRAY));
            }
            if (ENABLE_FILE_WATCHER) {
                player.sendMessage(text(""));
                player.sendMessage(text("Live Reload: ", YELLOW).append(text("ENABLED", GREEN)));
                player.sendMessage(text("Edit " + paintFile.getFileName() + " to see changes instantly!", GRAY));
            }
            player.sendMessage(text("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", DARK_GRAY));
        });

        // Cleanup on shutdown
        PaintFileWatcher finalFileWatcher = fileWatcher;
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            if (finalFileWatcher != null) {
                finalFileWatcher.close();
            }
        }));

        minecraftServer.start("0.0.0.0", 25565);
        LOGGER.info("Server started! Live reload features: file_watcher={}, commands={}",
                ENABLE_FILE_WATCHER, ENABLE_LOAD_COMMANDS);
    }
}
