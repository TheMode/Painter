package net.minestom.paint;

import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.TextComponent;
import net.kyori.adventure.text.format.NamedTextColor;
import net.kyori.adventure.text.format.TextColor;

import static net.kyori.adventure.text.Component.text;

final class PainterBranding {
    static final TextColor PRIMARY = TextColor.color(0xFF6C32);
    static final TextColor ACCENT = TextColor.color(0xFF76B6);
    static final NamedTextColor MUTED = NamedTextColor.DARK_GRAY;
    static final NamedTextColor NEUTRAL = NamedTextColor.GRAY;
    static final NamedTextColor SUCCESS = NamedTextColor.GREEN;
    static final NamedTextColor WARNING = NamedTextColor.GOLD;
    static final NamedTextColor FAILURE = NamedTextColor.RED;

    private PainterBranding() {
    }

    static Component gradientWord(String text) {
        return gradient(text, PRIMARY, ACCENT);
    }

    static Component gradient(String text, TextColor start, TextColor end) {
        if (text.isEmpty()) {
            return Component.empty();
        }
        TextComponent.Builder builder = Component.text();
        int length = text.length();
        for (int i = 0; i < length; i++) {
            float ratio = length == 1 ? 0 : (float) i / (length - 1);
            int red = Math.round(start.red() + (end.red() - start.red()) * ratio);
            int green = Math.round(start.green() + (end.green() - start.green()) * ratio);
            int blue = Math.round(start.blue() + (end.blue() - start.blue()) * ratio);
            builder.append(text(String.valueOf(text.charAt(i)), TextColor.color(red, green, blue)));
        }
        return builder.build();
    }

    static Component prefixed(Component message) {
        return Component.text()
                .append(gradientWord("Painter"))
                .append(text(" • ", NEUTRAL))
                .append(message)
                .build();
    }

    static Component separator() {
        return text("━━━━━━━━━━━━━━━━━━━━━━━━━━", MUTED);
    }
}
