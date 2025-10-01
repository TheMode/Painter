# Painter Live Reload Features

This document describes the live reload and sharing features added to the Painter Minestom demo server.

## Overview

The Painter demo server now supports **live reload** functionality similar to ShaderToy, making it an educational toy where you can:
- Edit `.paint` files and see changes instantly
- Load world generators from URLs with a single command
- Share your creations via pastebin/gist links
- Experiment and learn interactively

## Features

### 1. Automatic File Watching (Dev Mode)

When enabled, the server automatically watches the loaded `.paint` file for changes and reloads the generator when you save your edits.

**How it works:**
1. Edit your `.paint` file (e.g., `worlds/one_block.paint`)
2. Save the file
3. Players are briefly teleported to a waiting area
4. The server automatically reloads the generator
5. **All loaded chunks are regenerated immediately**
6. Players are teleported back to their exact positions
7. All players are notified
8. Changes are visible instantly - no need to explore new chunks!

**Configuration:**
```bash
-Dpainter.enableFileWatcher=true  # Default
```

### Player Safety During Reload

To prevent players from being disconnected when their chunk unloads during regeneration, the system automatically:
- Creates a safe "waiting instance" with a simple platform
- Teleports all players to the waiting area before unloading chunks
- Saves each player's exact position (including rotation)
- Regenerates all chunks with the new generator
- Teleports players back to their original positions
- The entire process takes only 1-2 seconds!

This ensures a smooth experience where players see the world transform around them without interruption.

### 2. Manual Reload Command

Manually reload the generator from the original file.

**Usage:**
```
/reload
```

**What happens:**
- Reads the `.paint` file from disk
- Compiles and loads the new generator
- **Regenerates ALL currently loaded chunks** (not just nearby)
- Shows success/error messages with chunk count

**Use cases:**
- When auto-reload is disabled
- To verify file-based changes
- After reverting file changes
- Instant feedback on your edits

### 3. Load from URL Command (ShaderToy Mode!)

Load a world generator from any URL - this is the "ShaderToy" feature that enables instant sharing.

**Usage:**
```
/loadpaint <url>
```

**Examples:**
```
/loadpaint pastebin.com/ABC123
/loadpaint gist.github.com/username/abc123def456
/loadpaint https://pastebin.com/raw/ABC123
```

**Supported services:**
- **Pastebin**: Automatically converts to raw URL
- **GitHub Gist**: Automatically converts to raw URL
- **Any raw URL**: Direct access

**What happens:**
- Downloads the `.paint` code from the URL
- Compiles and loads the new generator
- **Regenerates ALL currently loaded chunks**
- Shows the source URL, chunk count, and success/error messages
- Changes are visible immediately!

**Use cases:**
- Trying someone else's world generator
- Sharing your creations with others
- Educational demonstrations
- Community challenges/contests

### 4. Configuration

Both features can be enabled/disabled via system properties:

```bash
# Enable/disable automatic file watching (default: true)
-Dpainter.enableFileWatcher=true

# Enable/disable /reload and /loadpaint commands (default: true)
-Dpainter.enableLoadCommands=true
```

**Example usage:**
```bash
# Disable file watcher for production
java -Dpainter.enableFileWatcher=false -jar server.jar

# Disable all live reload features
java -Dpainter.enableFileWatcher=false -Dpainter.enableLoadCommands=false -jar server.jar
```

**Recommended configurations:**

| Scenario | File Watcher | Commands | Command |
|----------|-------------|----------|---------|
| Local development | ✅ Enabled | ✅ Enabled | Default (no flags needed) |
| Public creative server | ❌ Disabled | ✅ Enabled | `-Dpainter.enableFileWatcher=false` |
| Production server | ❌ Disabled | ❌ Disabled | Both flags set to `false` |
| Educational workshop | ✅ Enabled | ✅ Enabled | Default (no flags needed) |

## Architecture

### Components

1. **GeneratorReloader** (`GeneratorReloader.java`)
   - Static helper for reloading generators
   - **Safely teleports players to waiting instance during reload**
   - Changes instance generator and regenerates all loaded chunks
   - **Returns players to their original positions after reload**
   - Handles chunk tracking and batch regeneration

2. **PaintFileWatcher** (`PaintFileWatcher.java`)
   - Monitors file system for changes
   - Debounces rapid changes
   - Runs in background thread

3. **PaintUrlLoader** (`PaintUrlLoader.java`)
   - HTTP client for fetching remote code
   - URL preprocessing for common paste services
   - Size limits and timeout protection

4. **ReloadCommand** (`ReloadCommand.java`)
   - Minestom command handler
   - Full chunk regeneration logic
   - Player feedback

5. **LoadPaintCommand** (`LoadPaintCommand.java`)
   - Minestom command handler
   - Async URL loading
   - Full chunk regeneration logic

### Thread Safety

- **Generation**: The new generator is set atomically before chunk regeneration begins
- **Player Safety**: Players are moved to a waiting instance before chunks unload (prevents disconnection)
- **Reload**: All loaded chunks are tracked and regenerated with the new generator
- **Position Preservation**: Player positions are saved and restored after reload
- **Cleanup**: Minestom handles generator lifecycle automatically

### Error Handling

- Compilation errors show descriptive messages to players
- Network failures are logged and reported
- Invalid URLs are rejected with helpful feedback
- File watch errors don't crash the server

## Sharing Your Creations

### Quick Start Guide

1. **Create your world generator** in a `.paint` file
2. **Test it locally** with the file watcher
3. **Upload to a paste service**:
   - Go to [pastebin.com](https://pastebin.com) or [gist.github.com](https://gist.github.com)
   - Paste your `.paint` code
   - Copy the URL
4. **Share the link** with others
5. **Others can try it** with `/loadpaint <your-url>`

### Best Practices for Sharing

- **Add comments** explaining what your generator does
- **Start simple** - use basic blocks first
- **Test before sharing** - make sure it compiles
- **Document parameters** - if using macros, explain the values
- **Credit inspiration** - mention if based on others' work

### Example Shareable Generators

**Simple colored blocks:**
```paint
// Rainbow columns
for x in 0..16 {
  color = x % 7
  [x 0] concrete[color=color]
}
```

**Sphere showcase:**
```paint
// Sphere collection
#sphere .x=0 .y=10 .radius=5 .block=stone
#sphere .x=10 .y=10 .radius=3 .block=diamond_block
#sphere .x=-10 .y=10 .radius=7 .block=glass
```

## Use Cases

### 1. Educational Workshops
- Teacher prepares example generators
- Students load and modify them
- Instant feedback encourages experimentation
- Share best results with the class

### 2. Community Events
- "Generator of the Week" contests
- Theme-based challenges (e.g., "best terrain", "most creative structure")
- Collaborative building with shared generators
- Live coding demonstrations

### 3. Development & Testing
- Rapid iteration without server restarts
- Test different algorithms quickly
- Debug generation issues
- Performance comparison

### 4. Creative Playground
- Experiment with different ideas
- Learn from others' generators
- Combine multiple approaches
- Build a library of reusable patterns

## Limitations

- **No persistence**: URL-loaded generators are not saved to disk
- **Single instance**: Only one generator active at a time (per world)
- **Full regeneration**: All loaded chunks are regenerated on reload (may cause brief lag)
- **Security**: No sandboxing - only use trusted URLs
- **Size limit**: 1MB max for URL content

## Future Enhancements

Possible improvements:
- Gallery/library of community generators
- Vote/rating system for shared generators
- Generator versioning and history
- Multiple simultaneous generators (different worlds)
- Web UI for browser-based editing
- Syntax highlighting and error highlighting
- Real-time collaboration features

## Troubleshooting

**File watcher not working:**
- Check file path is correct
- Ensure file is saved (not just modified in editor)
- Look for error messages in server console
- Try `/reload` manually

**URL loading fails:**
- Verify URL is accessible in browser
- Check network connectivity
- Ensure URL points to raw content
- Try the raw URL directly

**Compilation errors:**
- Check `.paint` syntax
- Review error message carefully
- Test locally before sharing
- Use simpler examples to isolate issues

**Chunks not updating:**
- Changes should be immediate after reload
- Check server console for error messages
- Verify the reload completed successfully
- Try `/reload` manually if auto-reload seems stuck

## Security Considerations

**For server operators:**
- Only enable on trusted servers
- Consider disabling URL loading on public servers
- Monitor for abuse (excessive reloads)
- Validate file permissions
- Review loaded code before production use

**For users:**
- Only load URLs from trusted sources
- Review code before executing
- Report suspicious generators
- Use test servers for experiments

## Contributing

Ideas for improvements? Found a bug? Contributions welcome!

- Report issues on GitHub
- Share your cool generators
- Suggest new features
- Submit pull requests
