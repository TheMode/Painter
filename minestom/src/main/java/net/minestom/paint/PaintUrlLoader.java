package net.minestom.paint;

import org.jetbrains.annotations.NotNullByDefault;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

/**
 * Utility for loading .paint code from URLs.
 * Supports various paste services and raw URLs.
 */
@NotNullByDefault
public final class PaintUrlLoader {
    private static final Logger logger = LoggerFactory.getLogger(PaintUrlLoader.class);
    private static final Duration TIMEOUT = Duration.ofSeconds(10);
    private static final int MAX_SIZE = 1024 * 1024; // 1MB max

    private final HttpClient httpClient;

    public PaintUrlLoader() {
        this.httpClient = HttpClient.newBuilder()
                .connectTimeout(TIMEOUT)
                .followRedirects(HttpClient.Redirect.NORMAL)
                .build();
    }

    /**
     * Load painter code from a URL.
     * Automatically converts certain URLs to their raw form:
     * - pastebin.com -> pastebin.com/raw/
     * - gist.github.com -> gist.githubusercontent.com/.../raw/
     *
     * @param url The URL to load from
     * @return The painter program code
     * @throws IOException if the request fails or times out
     * @throws IllegalArgumentException if the response is too large
     */
    public String loadFromUrl(String url) throws IOException, InterruptedException {
        String processedUrl = processUrl(url);
        logger.info("Loading painter code from: {}", processedUrl);

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(processedUrl))
                .timeout(TIMEOUT)
                .GET()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());

        if (response.statusCode() != 200) {
            throw new IOException("HTTP " + response.statusCode() + " when fetching " + processedUrl);
        }

        String content = response.body();
        if (content.length() > MAX_SIZE) {
            throw new IllegalArgumentException("Response too large: " + content.length() + " bytes (max " + MAX_SIZE + ")");
        }

        logger.info("Successfully loaded {} bytes from {}", content.length(), processedUrl);
        return content;
    }

    /**
     * Convert various paste URLs to their raw form for easier sharing.
     */
    private String processUrl(String url) {
        // Pastebin: pastebin.com/ABC123 -> pastebin.com/raw/ABC123
        if (url.contains("pastebin.com/") && !url.contains("/raw/")) {
            url = url.replace("pastebin.com/", "pastebin.com/raw/");
            logger.debug("Converted to pastebin raw URL: {}", url);
        }

        // GitHub Gist: gist.github.com/user/id -> gist.githubusercontent.com/user/id/raw/
        if (url.contains("gist.github.com/") && !url.contains("githubusercontent")) {
            url = url.replace("gist.github.com/", "gist.githubusercontent.com/");
            if (!url.endsWith("/raw")) {
                url = url + "/raw";
            }
            logger.debug("Converted to gist raw URL: {}", url);
        }

        // Ensure https://
        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            url = "https://" + url;
        }

        return url;
    }
}
