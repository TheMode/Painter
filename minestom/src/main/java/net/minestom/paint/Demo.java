package net.minestom.paint;

import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.TextComponent;
import net.kyori.adventure.text.format.TextColor;
import net.minestom.server.MinecraftServer;
import net.minestom.server.adventure.audience.Audiences;
import net.minestom.server.command.CommandManager;
import net.minestom.server.coordinate.Pos;
import net.minestom.server.entity.GameMode;
import net.minestom.server.entity.Player;
import net.minestom.server.event.GlobalEventHandler;
import net.minestom.server.event.player.AsyncPlayerConfigurationEvent;
import net.minestom.server.event.player.PlayerSpawnEvent;
import net.minestom.server.event.server.ServerListPingEvent;
import net.minestom.server.extras.lan.OpenToLAN;
import net.minestom.server.extras.lan.OpenToLANConfig;
import net.minestom.server.instance.InstanceContainer;
import net.minestom.server.instance.InstanceManager;
import net.minestom.server.instance.LightingChunk;
import net.minestom.server.ping.Status;
import net.minestom.server.utils.time.TimeUnit;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;

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

    private static final Component SHOWCASE_MOTD = Component.text()
            .append(gradientText("Painter", TextColor.color(0xFF6C32), TextColor.color(0xFF76B6)))
            .append(text(" showcase", GRAY))
            .build();

    static void main(String[] args) throws IOException {
        MinecraftServer minecraftServer = MinecraftServer.init();

        // Create the instance
        InstanceManager instanceManager = MinecraftServer.getInstanceManager();
        InstanceContainer instance = instanceManager.createInstanceContainer();
        instance.setTimeRate(0);
        instance.setTime(12000);
        instance.setChunkSupplier(LightingChunk::new);

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
                        GeneratorReloader.reload(instance, content, source);
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
            commandManager.register(new PainterCommand(instance, paintFile));
            LOGGER.info("Commands registered: /painter reload, /painter load <url>");
        }

        // Add an event callback to specify the spawning instance (and the spawn position)
        GlobalEventHandler globalEventHandler = MinecraftServer.getGlobalEventHandler();
        globalEventHandler.addListener(ServerListPingEvent.class, event ->
                event.setStatus(Status.builder().description(SHOWCASE_MOTD).build()));
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
                player.sendMessage(text("  /painter reload", GREEN).append(text(" - Reload from file", GRAY)));
                player.sendMessage(text("  /painter load <url>", GREEN).append(text(" - Load from URL", GRAY)));
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

        OpenToLAN.open(new OpenToLANConfig().eventCallDelay(Duration.of(1, TimeUnit.DAY)));

        minecraftServer.start("0.0.0.0", 25565);
        LOGGER.info("Server started! Live reload features: file_watcher={}, commands={}",
                ENABLE_FILE_WATCHER, ENABLE_LOAD_COMMANDS);
    }

    private static Component gradientText(String text, TextColor start, TextColor end) {
        if (text.isEmpty()) {
            return Component.empty();
        }
        int length = text.length();
        TextComponent.Builder builder = Component.text();
        for (int i = 0; i < length; i++) {
            float ratio = length == 1 ? 0 : (float) i / (length - 1);
            int red = Math.round(start.red() + (end.red() - start.red()) * ratio);
            int green = Math.round(start.green() + (end.green() - start.green()) * ratio);
            int blue = Math.round(start.blue() + (end.blue() - start.blue()) * ratio);
            builder.append(Component.text(String.valueOf(text.charAt(i)), TextColor.color(red, green, blue)));
        }
        return builder.build();
    }
}
