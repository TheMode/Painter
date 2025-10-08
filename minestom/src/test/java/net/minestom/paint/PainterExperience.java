package net.minestom.paint;

import net.kyori.adventure.audience.Audience;
import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.TextComponent;
import net.kyori.adventure.text.format.NamedTextColor;
import net.minestom.server.MinecraftServer;
import net.minestom.server.entity.Player;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.file.Path;

import static net.kyori.adventure.text.Component.text;

final class PainterExperience implements AutoCloseable {
    private static final Logger LOGGER = LoggerFactory.getLogger(PainterExperience.class);

    private final Path paintFile;
    private final boolean liveReloadEnabled;
    private final boolean commandsEnabled;
    private boolean liveReloadActive;

    PainterExperience(Path paintFile, boolean liveReloadEnabled, boolean commandsEnabled) {
        this.paintFile = paintFile;
        this.liveReloadEnabled = liveReloadEnabled;
        this.commandsEnabled = commandsEnabled;
        this.liveReloadActive = liveReloadEnabled;
    }

    void initialize() {
        updatePlayerListFormatting();
    }

    void handleFirstSpawn(Player player) {
        player.sendMessage(PainterBranding.separator());
        player.sendMessage(PainterBranding.prefixed(
                text("Welcome " + player.getUsername() + "!", NamedTextColor.WHITE)));
        player.sendMessage(PainterBranding.prefixed(
                text("Explore the generated world and tweak the paint program live.", PainterBranding.NEUTRAL)));
        if (commandsEnabled) {
            player.sendMessage(PainterBranding.prefixed(
                    text("Try ", PainterBranding.NEUTRAL)
                            .append(text("/painter reload", PainterBranding.SUCCESS))
                            .append(text(" after editing " + paintFile.getFileName(), PainterBranding.NEUTRAL))));
            player.sendMessage(PainterBranding.prefixed(
                    text("Use ", PainterBranding.NEUTRAL)
                            .append(text("/painter load <url>", PainterBranding.SUCCESS))
                            .append(text(" to showcase your own scripts.", PainterBranding.NEUTRAL))));
        }
        if (liveReloadEnabled) {
            player.sendMessage(PainterBranding.prefixed(
                    text("File watcher is active — save your .paint file to regenerate instantly.", PainterBranding.NEUTRAL)));
        }
        player.sendMessage(PainterBranding.separator());
        updatePlayerListFormatting();
    }

    void handleRespawn(Player player) {
        // nothing to reattach
        updatePlayerListFormatting();
    }

    void handleDisconnect(Player player) {
        // nothing to clean up
        updatePlayerListFormatting();
    }

    void notifyReloadStarted(String source) {
        runOnMainThread(() -> onlinePlayersAudience().sendActionBar(
                PainterBranding.prefixed(text("Reloading generator…", PainterBranding.WARNING))
        ));
    }

    void notifyReloadSuccess(String source) {
        runOnMainThread(() -> onlinePlayersAudience().sendMessage(
                PainterBranding.prefixed(text("World generator reloaded from " + sourceDescriptor(source), PainterBranding.SUCCESS))
        ));
    }

    void notifyReloadFailure(String source, String errorMessage) {
        runOnMainThread(() -> {
            String sanitized = (errorMessage == null || errorMessage.isBlank())
                    ? "see logs"
                    : shorten(errorMessage, 64);
            onlinePlayersAudience().sendMessage(
                    PainterBranding.prefixed(text("Reload failed (" + sourceDescriptor(source) + "): " + sanitized, PainterBranding.FAILURE)));
        });
    }

    void notifyLiveReloadUnavailable(Throwable throwable) {
        runOnMainThread(() -> {
            LOGGER.warn("Live reload unavailable", throwable);
            onlinePlayersAudience().sendMessage(
                    PainterBranding.prefixed(text("Live reload disabled — see logs for details.", PainterBranding.FAILURE)));
            this.liveReloadActive = false;
            updatePlayerListFormatting();
        });
    }

    @Override
    public void close() {
        // nothing to close
    }

    private void runOnMainThread(Runnable runnable) {
        MinecraftServer.getSchedulerManager().scheduleNextTick(runnable);
    }

    private static String shorten(String text, int maxLength) {
        if (text.length() <= maxLength) {
            return text;
        }
        return text.substring(0, Math.max(0, maxLength - 1)) + "…";
    }

    private String sourceDescriptor(String source) {
        if (source == null || source.isBlank()) {
            return "unknown";
        }
        return shorten(source, 30);
    }

    private Audience onlinePlayersAudience() {
        return Audience.audience(MinecraftServer.getConnectionManager().getOnlinePlayers());
    }

    private void updatePlayerListFormatting() {
        Component header = playerListHeader();
        Component footer = playerListFooter();
        runOnMainThread(() -> onlinePlayersAudience().sendPlayerListHeaderAndFooter(header, footer));
    }

    private Component playerListHeader() {
        return Component.text()
                .append(PainterBranding.gradientWord("Painter Demo"))
                .append(Component.newline())
                .append(text("Procedural worlds in seconds", PainterBranding.NEUTRAL))
                .build();
    }

    private Component playerListFooter() {
        TextComponent.Builder builder = Component.text()
                .append(text("Program: ", PainterBranding.NEUTRAL))
                .append(text(paintFile.getFileName().toString(), NamedTextColor.WHITE))
                .append(Component.newline())
                .append(text("Live Reload: ", PainterBranding.NEUTRAL))
                .append(text(liveReloadStatus(), liveReloadActive ? PainterBranding.SUCCESS : PainterBranding.FAILURE));
        if (commandsEnabled) {
            builder.append(Component.newline())
                    .append(text("/painter reload", PainterBranding.SUCCESS))
                    .append(text(" • ", PainterBranding.NEUTRAL))
                    .append(text("/painter load <url>", PainterBranding.SUCCESS));
        }
        return builder.build();
    }

    private String liveReloadStatus() {
        if (!liveReloadEnabled) {
            return "disabled";
        }
        return liveReloadActive ? "enabled" : "unavailable";
    }
}
