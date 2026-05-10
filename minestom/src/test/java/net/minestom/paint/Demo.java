package net.minestom.paint;

import me.tongfei.progressbar.DelegatingProgressBarConsumer;
import me.tongfei.progressbar.ProgressBar;
import me.tongfei.progressbar.ProgressBarBuilder;
import me.tongfei.progressbar.ProgressBarStyle;
import net.kyori.adventure.nbt.CompoundBinaryTag;
import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.format.NamedTextColor;
import net.minestom.server.MinecraftServer;
import net.minestom.server.ServerFlag;
import net.minestom.server.command.CommandManager;
import net.minestom.server.coordinate.ChunkRange;
import net.minestom.server.coordinate.Pos;
import net.minestom.server.entity.GameMode;
import net.minestom.server.entity.Player;
import net.minestom.server.event.GlobalEventHandler;
import net.minestom.server.event.player.AsyncPlayerConfigurationEvent;
import net.minestom.server.event.player.PlayerCustomClickEvent;
import net.minestom.server.event.player.PlayerDisconnectEvent;
import net.minestom.server.event.player.PlayerSpawnEvent;
import net.minestom.server.event.server.ServerListPingEvent;
import net.minestom.server.extras.lan.OpenToLAN;
import net.minestom.server.extras.lan.OpenToLANConfig;
import net.minestom.server.instance.Chunk;
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
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;

import static net.kyori.adventure.text.Component.text;

public final class Demo {
    private static final Logger LOGGER = LoggerFactory.getLogger(Demo.class);
    private static final Component SHOWCASE_MOTD = Component.text()
            .append(PainterBranding.gradientWord("Painter"))
            .append(text(" showcase", NamedTextColor.GRAY))
            .build();
    private static final Pos SPAWN_POS = new Pos(0, 100, 0);

    private Demo() {
    }

    static void main() throws IOException {
        start();
    }

    private static void start() throws IOException {
        MinecraftServer server = MinecraftServer.init();
        DemoConfig config = loadConfig();

        InstanceContainer instance = createInstance(MinecraftServer.getInstanceManager());
        PainterExperience experience = new PainterExperience(
                config.paintFile(),
                config.enableFileWatcher(),
                config.enableLoadCommands()
        );

        loadInitialGenerator(instance, config.paintFile(), experience);

        PaintFileWatcher watcher = setupFileWatcher(instance, config, experience);
        registerCommands(instance, config, experience);
        registerEventHandlers(instance, config, experience);
        registerShutdownHooks(watcher, experience);

        exposeToLan();

        server.start(config.bindAddress(), config.port());
        LOGGER.info("Server started on {}:{} (fileWatcher={}, commands={})",
                config.bindAddress(), config.port(), config.enableFileWatcher(), config.enableLoadCommands());
    }

    private static DemoConfig loadConfig() {
        Path program = Path.of(System.getProperty("painter.program", "worlds/one_block.paint"));
        boolean fileWatcher = Boolean.parseBoolean(System.getProperty("painter.enableFileWatcher", "true"));
        boolean loadCommands = Boolean.parseBoolean(System.getProperty("painter.enableLoadCommands", "true"));
        String bindAddress = System.getProperty("painter.bindAddress", "0.0.0.0");
        int port = Integer.parseInt(System.getProperty("painter.port", "25565"));

        return new DemoConfig(program, fileWatcher, loadCommands, bindAddress, port);
    }

    private static InstanceContainer createInstance(InstanceManager manager) {
        InstanceContainer instance = manager.createInstanceContainer();
        instance.setTimeRate(0);
        instance.setTime(12000);
        instance.setChunkSupplier(LightingChunk::new);
        return instance;
    }

    private static void loadInitialGenerator(InstanceContainer instance, Path paintFile, PainterExperience experience) throws IOException {
        String program = Files.readString(paintFile);
        PaintGenerator generator = PaintGenerator.load(program);
        instance.setGenerator(generator);
        experience.initialize();
        experience.notifyReloadSuccess("initial program");

        final int chunkCount = ChunkRange.chunksCount(ServerFlag.CHUNK_VIEW_DISTANCE);
        LOGGER.info("Pre-loading {} chunks around spawn", chunkCount);
        List<CompletableFuture<Chunk>> futures = new ArrayList<>();
        ChunkRange.chunksInRange(SPAWN_POS, ServerFlag.CHUNK_VIEW_DISTANCE, (chunkX, chunkZ) -> futures.add(instance.loadChunk(chunkX, chunkZ)));
        long startNanos = System.nanoTime();

        try (ProgressBar pb = new ProgressBarBuilder()
                .setTaskName("Loading chunks")
                .setInitialMax(futures.size())
                .setStyle(ProgressBarStyle.ASCII)
                .setConsumer(new DelegatingProgressBarConsumer(LOGGER::info))
                .build()) {

            for (CompletableFuture<Chunk> future : futures) {
                future.thenRun(pb::step);
            }
            CompletableFuture.allOf(futures.toArray(CompletableFuture[]::new)).join();
        }

        long elapsedNanos = System.nanoTime() - startNanos;
        int loaded = futures.size();
        long avgNanosPerChunk = loaded > 0 ? (elapsedNanos / loaded) : 0L;
        LOGGER.info("Pre-loaded {} chunks around spawn in {} (avg {} per chunk)", loaded,
                formatDuration(elapsedNanos), formatDuration(avgNanosPerChunk));
    }

    private static String formatDuration(long nanos) {
        if (nanos >= 60L * 1_000_000_000L) {
            double mins = nanos / 1_000_000_000.0 / 60.0;
            return String.format("%.2f min", mins);
        } else if (nanos >= 1_000_000_000L) {
            double secs = nanos / 1_000_000_000.0;
            return String.format("%.2f s", secs);
        } else if (nanos >= 1_000_000L) {
            double ms = nanos / 1_000_000.0;
            return String.format("%.2f ms", ms);
        } else if (nanos >= 1_000L) {
            double us = nanos / 1_000.0;
            return String.format("%.2f us", us);
        } else {
            return nanos + " ns";
        }
    }

    private static PaintFileWatcher setupFileWatcher(InstanceContainer instance, DemoConfig config, PainterExperience experience) {
        if (!config.enableFileWatcher()) {
            return null;
        }
        try {
            PaintFileWatcher watcher = new PaintFileWatcher(config.paintFile(), (content, source) -> {
                experience.notifyReloadStarted(source);
                try {
                    GeneratorReloader.reload(instance, content, source);
                    experience.notifyReloadSuccess(source);
                    LOGGER.info("Auto-reloaded generator from {}", source);
                } catch (Exception e) {
                    LOGGER.error("Failed to auto-reload generator", e);
                    experience.notifyReloadFailure(source, e.getMessage());
                }
            });
            LOGGER.info("Live reload enabled for {}", config.paintFile());
            return watcher;
        } catch (IOException e) {
            experience.notifyLiveReloadUnavailable(e);
            LOGGER.warn("Failed to start file watcher for {}", config.paintFile(), e);
            return null;
        }
    }

    private static void registerCommands(InstanceContainer instance, DemoConfig config, PainterExperience experience) {
        if (!config.enableLoadCommands()) {
            return;
        }
        CommandManager commands = MinecraftServer.getCommandManager();
        commands.register(
                new PainterCommand(instance, config.paintFile(), experience),
                new TeleportCommand()
        );
        LOGGER.info("Commands registered: /painter reload, /painter load <url>, /painter dialog");
    }

    private static void registerEventHandlers(InstanceContainer instance, DemoConfig config, PainterExperience experience) {
        GlobalEventHandler events = MinecraftServer.getGlobalEventHandler();
        events.addListener(ServerListPingEvent.class, event -> {
            final int online = MinecraftServer.getConnectionManager().getOnlinePlayers().size();
            event.setStatus(Status.builder()
                    .description(SHOWCASE_MOTD)
                    .playerInfo(online, 0)
                    .build());
        });
        events.addListener(AsyncPlayerConfigurationEvent.class, event -> {
            Player player = event.getPlayer();
            event.setSpawningInstance(instance);
            player.setRespawnPoint(SPAWN_POS);
            player.setGameMode(GameMode.CREATIVE);
        });
        events.addListener(PlayerSpawnEvent.class, event -> {
            Player player = event.getPlayer();
            if (event.isFirstSpawn()) {
                experience.handleFirstSpawn(player);
            } else {
                experience.handleRespawn(player);
            }
        });
        events.addListener(PlayerDisconnectEvent.class, event -> experience.handleDisconnect(event.getPlayer()));
        events.addListener(PlayerCustomClickEvent.class, e -> {
            if (!e.getKey().equals(PainterCommand.DIALOG_RUN_KEY)) return;
            Player p = e.getPlayer();
            if (!(e.getPayload() instanceof CompoundBinaryTag nbt)) {
                p.sendMessage(text("✗ Invalid dialog response.", NamedTextColor.RED));
                return;
            }
            String code = nbt.getString(PainterCommand.DIALOG_INPUT_KEY);
            CompletableFuture.runAsync(() -> {
                try {
                    experience.notifyReloadStarted("dialog");
                    GeneratorReloader.reload(instance, code, "dialog");
                    experience.notifyReloadSuccess("dialog");
                } catch (Exception ex) {
                    LOGGER.error("Dialog reload failed", ex);
                    experience.notifyReloadFailure("dialog", ex.getMessage());
                    p.sendMessage(text("✗ " + ex.getMessage(), NamedTextColor.RED));
                }
            });
        });
    }

    private static void registerShutdownHooks(PaintFileWatcher watcher, PainterExperience experience) {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            if (watcher != null) watcher.close();
            experience.close();
        }));
    }

    private static void exposeToLan() {
        OpenToLAN.open(new OpenToLANConfig().eventCallDelay(Duration.of(1, TimeUnit.DAY)));
    }

    private record DemoConfig(
            Path paintFile,
            boolean enableFileWatcher,
            boolean enableLoadCommands,
            String bindAddress, int port
    ) {
    }
}
