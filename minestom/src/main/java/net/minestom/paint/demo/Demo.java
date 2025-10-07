package net.minestom.paint.demo;

import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.format.NamedTextColor;
import net.minestom.paint.GeneratorReloader;
import net.minestom.paint.PaintFileWatcher;
import net.minestom.paint.PaintGenerator;
import net.minestom.server.MinecraftServer;
import net.minestom.server.command.CommandManager;
import net.minestom.server.coordinate.Pos;
import net.minestom.server.entity.GameMode;
import net.minestom.server.entity.Player;
import net.minestom.server.event.GlobalEventHandler;
import net.minestom.server.event.player.AsyncPlayerConfigurationEvent;
import net.minestom.server.event.player.PlayerDisconnectEvent;
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

public final class Demo {
    private static final Logger LOGGER = LoggerFactory.getLogger(Demo.class);
    private static final Component SHOWCASE_MOTD = Component.text()
            .append(PainterBranding.gradientWord("Painter"))
            .append(text(" showcase", NamedTextColor.GRAY))
            .build();

    private Demo() {
    }

    static void main(String[] args) throws IOException {
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
        Path program = Path.of(System.getProperty("painter.program", "worlds/plain_forest.paint"));
        boolean fileWatcher = Boolean.parseBoolean(System.getProperty("painter.enableFileWatcher", "true"));
        boolean loadCommands = Boolean.parseBoolean(System.getProperty("painter.enableLoadCommands", "true"));
        String bindAddress = System.getProperty("painter.bindAddress", "0.0.0.0");
        int port = Integer.parseInt(System.getProperty("painter.port", "25565"));
        int maxPlayers = Integer.parseInt(System.getProperty("painter.maxPlayers", "20"));

        return new DemoConfig(program, fileWatcher, loadCommands, bindAddress, port, maxPlayers);
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
        instance.loadChunk(0, 0).join();
        LOGGER.info("Initial generator loaded from {}", paintFile);
        experience.initialize();
        experience.notifyReloadSuccess("initial program");
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
        LOGGER.info("Commands registered: /painter reload, /painter load <url>");
    }

    private static void registerEventHandlers(InstanceContainer instance, DemoConfig config, PainterExperience experience) {
        GlobalEventHandler events = MinecraftServer.getGlobalEventHandler();
        events.addListener(ServerListPingEvent.class, event -> {
            int online = MinecraftServer.getConnectionManager().getOnlinePlayers().size();
            event.setStatus(Status.builder()
                    .description(SHOWCASE_MOTD)
                    .playerInfo(config.maxPlayers(), online)
                    .build());
        });
        events.addListener(AsyncPlayerConfigurationEvent.class, event -> {
            Player player = event.getPlayer();
            event.setSpawningInstance(instance);
            player.setRespawnPoint(new Pos(0, 100, 0));
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
    }

    private static void registerShutdownHooks(PaintFileWatcher watcher, PainterExperience experience) {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            if (watcher != null) {
                watcher.close();
            }
            experience.close();
        }));
    }

    private static void exposeToLan() {
        OpenToLAN.open(new OpenToLANConfig().eventCallDelay(Duration.of(1, TimeUnit.DAY)));
    }

    private record DemoConfig(Path paintFile, boolean enableFileWatcher, boolean enableLoadCommands,
                              String bindAddress, int port, int maxPlayers) {
    }
}
