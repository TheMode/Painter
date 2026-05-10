package net.minestom.paint;

import net.kyori.adventure.key.Key;
import net.kyori.adventure.text.Component;
import net.minestom.server.command.builder.Command;
import net.minestom.server.command.builder.arguments.ArgumentType;
import net.minestom.server.command.builder.arguments.ArgumentWord;
import net.minestom.server.dialog.Dialog;
import net.minestom.server.dialog.DialogAction;
import net.minestom.server.dialog.DialogActionButton;
import net.minestom.server.dialog.DialogAfterAction;
import net.minestom.server.dialog.DialogBody;
import net.minestom.server.dialog.DialogInput;
import net.minestom.server.dialog.DialogMetadata;
import net.minestom.server.entity.Player;
import net.minestom.server.instance.InstanceContainer;
import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.CompletableFuture;

import static net.kyori.adventure.text.Component.text;
import static net.kyori.adventure.text.format.NamedTextColor.*;

/**
 * Root command {@code /painter} with subcommands for loading and editing world generator programs.
 *
 * <table border="1" summary="Painter subcommands">
 *   <caption>Subcommands</caption>
 *   <tr><th>Command</th><th>Description</th></tr>
 *   <tr><td>{@code /painter reload}</td><td>Reload generator from the configured {@code .paint} file on disk.</td></tr>
 *   <tr><td>{@code /painter load <url>}</td><td>Fetch program text from a URL and regenerate the world.</td></tr>
 *   <tr><td>{@code /painter dialog}</td><td>Open the vanilla dialog with a multiline editor (players only).</td></tr>
 * </table>
 *
 * <p>Reload and URL load regenerate all chunks currently loaded in the instance.</p>
 */
@NotNullByDefault
public final class PainterCommand extends Command {

    /**
     * Custom click id for the dialog Run button; must match {@link net.minestom.server.event.player.PlayerCustomClickEvent#getKey()}.
     */
    static final Key DIALOG_RUN_KEY = Key.key("painter", "run");

    /**
     * NBT compound field name for the multiline source field (client submits input values under each control's key).
     */
    static final String DIALOG_INPUT_KEY = "program";

    public PainterCommand(InstanceContainer instance, Path path, PainterExperience experience) {
        super("painter");
        addSubcommands(new Load(instance, experience), new Reload(instance, path, experience), new Editor(path));
    }

    /**
     * Builds the in-game dialog: multiline text keyed {@link #DIALOG_INPUT_KEY}, Run action {@link #DIALOG_RUN_KEY}.
     *
     * @param initial text shown in the editor (often the current file contents)
     */
    static Dialog.MultiAction programDialog(String initial) {
        String src = initial == null ? "" : initial;
        DialogMetadata meta = new DialogMetadata(
                Component.text("Painter program"),
                null,
                true,
                true,
                DialogAfterAction.CLOSE,
                List.of(new DialogBody.PlainMessage(text("Edit below, then Run."), DialogBody.PlainMessage.DEFAULT_WIDTH)),
                List.of(new DialogInput.Text(
                        DIALOG_INPUT_KEY,
                        DialogInput.DEFAULT_WIDTH * 2,
                        text("Source"),
                        true,
                        src,
                        Integer.MAX_VALUE,
                        new DialogInput.Text.Multiline(24, null))));
        var run = new DialogActionButton(
                text("Run"),
                null,
                DialogActionButton.DEFAULT_WIDTH,
                new DialogAction.DynamicCustom(DIALOG_RUN_KEY, null));
        return new Dialog.MultiAction(meta, List.of(run), null, 1);
    }

    /**
     * Loads generator source from a URL.
     * <p>
     * Usage: {@code /painter load <url>} — supports Pastebin, GitHub Gist, and raw URLs (see {@link PaintUrlLoader}).
     * Regenerates all currently loaded chunks; players may be moved to a waiting instance during reload.
     */
    private static final class Load extends Command {
        private static final Logger LOGGER = LoggerFactory.getLogger(Load.class);
        private final PainterExperience experience;

        Load(InstanceContainer instance, PainterExperience experience) {
            super("load");
            this.experience = experience;
            PaintUrlLoader urlLoader = new PaintUrlLoader();
            ArgumentWord urlArg = ArgumentType.Word("url");
            addSyntax((sender, context) -> {
                final String url = context.get(urlArg);
                sender.sendMessage(text("Loading painter code from: " + url, YELLOW));
                experience.notifyReloadStarted("URL: " + url);
                CompletableFuture.runAsync(() -> {
                    try {
                        final String content = urlLoader.loadFromUrl(url);
                        int chunkCount = instance.getChunks().size();
                        int playerCount = instance.getPlayers().size();
                        sender.sendMessage(text("Regenerating " + chunkCount + " loaded chunks...", GRAY));
                        if (playerCount > 0) {
                            sender.sendMessage(text("Moving " + playerCount + " players to waiting area...", GRAY));
                        }
                        GeneratorReloader.reload(instance, content, "URL: " + url);
                        experience.notifyReloadSuccess("URL: " + url);
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
                        experience.notifyReloadFailure("URL: " + url, "interrupted");
                    } catch (Exception e) {
                        LOGGER.error("Failed to load from URL: {}", url, e);
                        sender.sendMessage(text("✗ Failed to load: " + e.getMessage(), RED));
                        experience.notifyReloadFailure("URL: " + url, e.getMessage());
                    }
                });
            }, urlArg);
            setDefaultExecutor((sender, context) -> {
                sender.sendMessage(text("Usage: /painter load <url>", RED));
                sender.sendMessage(text("Example: /painter load pastebin.com/ABC123", GRAY));
                sender.sendMessage(text("Supported: Pastebin, GitHub Gist, raw URLs", GRAY));
            });
        }
    }

    /**
     * Opens the vanilla dialog with a multiline Painter program editor, pre-filled from the demo paint file.
     * <p>
     * Usage: {@code /painter dialog} — players only. Submitting Run triggers {@link #DIALOG_RUN_KEY} on the server.
     */
    private static final class Editor extends Command {
        Editor(Path paintFile) {
            super("dialog");
            setDefaultExecutor((sender, context) -> {
                if (!(sender instanceof Player player)) {
                    sender.sendMessage(text("Players only.", RED));
                    return;
                }
                String initial;
                try {
                    initial = Files.readString(paintFile);
                } catch (IOException e) {
                    initial = "";
                }
                player.showDialog(programDialog(initial));
            });
        }
    }

    /**
     * Reloads the generator from the original {@code .paint} file path supplied at construction.
     * <p>
     * Usage: {@code /painter reload} — regenerates all currently loaded chunks; players may be moved to a waiting instance during reload.
     */
    public static final class Reload extends Command {
        private static final Logger LOGGER = LoggerFactory.getLogger(Reload.class);
        private final PainterExperience experience;

        public Reload(InstanceContainer instance, Path paintFile, PainterExperience experience) {
            super("reload");
            this.experience = experience;
            setDefaultExecutor((sender, context) -> {
                sender.sendMessage(text("Reloading world generator from file...", YELLOW));
                experience.notifyReloadStarted(paintFile.toString());
                try {
                    final String content = Files.readString(paintFile);
                    final int chunkCount = instance.getChunks().size();
                    final int playerCount = instance.getPlayers().size();
                    sender.sendMessage(text("Regenerating " + chunkCount + " loaded chunks...", GRAY));
                    if (playerCount > 0) {
                        sender.sendMessage(text("Moving " + playerCount + " players to waiting area...", GRAY));
                    }
                    GeneratorReloader.reload(instance, content, paintFile.toString());
                    experience.notifyReloadSuccess(paintFile.toString());
                    sender.sendMessage(text("✓ World generator reloaded successfully!", GREEN));
                    sender.sendMessage(text("✓ All " + chunkCount + " chunks regenerated!", GREEN));
                    if (playerCount > 0) {
                        sender.sendMessage(text("✓ Players returned to regenerated world!", GREEN));
                    }
                } catch (IOException e) {
                    LOGGER.error("Failed to read paint file", e);
                    sender.sendMessage(text("✗ Failed to read file: " + e.getMessage(), RED));
                    experience.notifyReloadFailure(paintFile.toString(), e.getMessage());
                } catch (Exception e) {
                    LOGGER.error("Failed to reload generator", e);
                    sender.sendMessage(text("✗ Failed to reload: " + e.getMessage(), RED));
                    experience.notifyReloadFailure(paintFile.toString(), e.getMessage());
                }
            });
        }
    }
}
