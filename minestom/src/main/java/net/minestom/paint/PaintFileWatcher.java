package net.minestom.paint;

import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.BiConsumer;

/**
 * Watches a .paint file for changes and triggers a callback when modifications are detected.
 * This enables live reload functionality during development.
 */
@NotNullByDefault
public final class PaintFileWatcher implements AutoCloseable {
    private static final Logger logger = LoggerFactory.getLogger(PaintFileWatcher.class);

    private final Path filePath;
    private final BiConsumer<String, String> onReload; // (content, source) -> void
    private final WatchService watchService;
    private final ExecutorService executor;
    private volatile boolean running = true;

    /**
     * Create a new file watcher.
     *
     * @param filePath The path to the .paint file to watch
     * @param onReload Callback invoked when the file changes, receives (fileContent, filePath)
     * @throws IOException if the watch service cannot be created
     */
    public PaintFileWatcher(Path filePath, BiConsumer<String, String> onReload) throws IOException {
        this.filePath = filePath.toAbsolutePath();
        this.onReload = onReload;
        this.watchService = FileSystems.getDefault().newWatchService();
        this.executor = Executors.newSingleThreadExecutor(r -> {
            Thread thread = new Thread(r, "PaintFileWatcher");
            thread.setDaemon(true);
            return thread;
        });

        // Watch the parent directory
        Path directory = this.filePath.getParent();
        directory.register(watchService, StandardWatchEventKinds.ENTRY_MODIFY);

        logger.info("Started watching file: {}", this.filePath);

        // Start watching in background thread
        executor.submit(this::watchLoop);
    }

    private void watchLoop() {
        while (running) {
            WatchKey key;
            try {
                key = watchService.take(); // Blocks until an event occurs
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }

            for (WatchEvent<?> event : key.pollEvents()) {
                WatchEvent.Kind<?> kind = event.kind();

                if (kind == StandardWatchEventKinds.OVERFLOW) {
                    continue;
                }

                @SuppressWarnings("unchecked")
                WatchEvent<Path> ev = (WatchEvent<Path>) event;
                Path changed = ev.context();

                // Check if the changed file is the one we're watching
                try {
                    Path fullPath = filePath.getParent().resolve(changed);
                    if (Files.isSameFile(fullPath, filePath)) {
                        handleFileChange();
                    }
                } catch (IOException e) {
                    logger.warn("Failed to check file path", e);
                }
            }

            // Reset the key to receive further events
            boolean valid = key.reset();
            if (!valid) {
                logger.warn("Watch key no longer valid, stopping file watcher");
                break;
            }
        }
    }

    private void handleFileChange() {
        logger.info("Detected change in file: {}", filePath);
        
        try {
            // Small delay to ensure file write is complete
            Thread.sleep(100);
            
            String content = Files.readString(filePath);
            onReload.accept(content, filePath.toString());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (IOException e) {
            logger.error("Failed to read file after change: {}", filePath, e);
        } catch (Exception e) {
            logger.error("Error during reload callback", e);
        }
    }

    @Override
    public void close() {
        running = false;
        executor.shutdownNow();
        try {
            watchService.close();
        } catch (IOException e) {
            logger.warn("Failed to close watch service", e);
        }
        logger.info("Stopped watching file: {}", filePath);
    }
}
