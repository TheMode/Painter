package net.minestom.paint;

import org.jetbrains.annotations.NotNullByDefault;
import org.jetbrains.annotations.Nullable;

import java.util.concurrent.atomic.AtomicReference;

/**
 * Text of the Painter program last successfully installed on the instance (any route: file, URL, dialog).
 * Used to pre-fill {@code /painter dialog} so the editor always reflects what is running.
 */
@NotNullByDefault
public final class ActivePaintSource {

    private static final AtomicReference<@Nullable String> ACTIVE = new AtomicReference<>();

    private ActivePaintSource() {
    }

    public static String get() {
        String s = ACTIVE.get();
        return s != null ? s : "";
    }

    static void set(String programSource) {
        ACTIVE.set(programSource);
    }
}
