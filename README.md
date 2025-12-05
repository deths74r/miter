# Miter

Miter is a lightweight terminal text editor written in C. It's a single-file implementation (~8,400 lines) with minimal dependencies, designed for simplicity and speed.

## Features

- **Multi-cursor editing** with Kitty terminal protocol support
- **Syntax highlighting** for C/C++
- **Undo/redo** with time-based grouping
- **Incremental search** with match highlighting
- **Mouse support** - click to position, drag to select, scroll wheel
- **File browser** - interactive half-screen panel (Ctrl+O)
- **102 color themes** including accessibility themes for colorblind users
- **Soft wrap** - visual line wrapping without modifying files
- **Selection and clipboard** - system clipboard integration via xclip/xsel
- **Bracket matching** - jump to matching bracket with Ctrl+]
- **Line numbers** with dynamic gutter

## Installation

### Build from Source

```bash
# Clone the repository
git clone https://github.com/deths74r/miter.git
cd miter

# Build (requires GCC and PCRE2)
make

# Optional: Install system-wide
sudo cp miter /usr/local/bin/
```

### Dependencies

| Platform | Dependencies |
|----------|--------------|
| Debian/Ubuntu | `sudo apt install build-essential libpcre2-dev` |
| Fedora/RHEL | `sudo dnf install gcc pcre2-devel` |
| Arch Linux | `sudo pacman -S base-devel pcre2` |
| macOS | `brew install pcre2` |

## Usage

```bash
./miter [filename]
```

## Keyboard Shortcuts

| Shortcut | Description |
|----------|-------------|
| **File Operations** | |
| Ctrl+O | Open file browser |
| Ctrl+S | Save file |
| Ctrl+Q | Quit (press 3x if unsaved changes) |
| **Undo/Redo** | |
| Ctrl+Z | Undo (grouped by typing pauses) |
| Ctrl+Y | Redo |
| **Navigation** | |
| Arrow keys | Move cursor |
| Home / End | Jump to start/end of line |
| Page Up / Page Down | Scroll by page |
| Ctrl+G | Go to line number |
| Ctrl+] | Jump to matching bracket |
| Alt+[ | Jump back to opening bracket |
| Alt+] | Skip to closing bracket |
| **Search** | |
| Ctrl+F | Find (incremental search) |
| Arrow keys (in search) | Navigate between results |
| Enter (in search) | Accept position |
| ESC (in search) | Cancel, restore position |
| **Selection** | |
| Shift + Arrow keys | Extend selection |
| Ctrl+A | Select all |
| Ctrl+C | Copy selection |
| Ctrl+X | Cut selection |
| Ctrl+V | Paste |
| **Text Manipulation** | |
| Alt+Q | Hard wrap paragraph at column 80 |
| Alt+J | Join/unwrap paragraph |
| Alt+W | Toggle soft wrap |
| **Word Operations** | |
| Ctrl+Left | Move to previous word |
| Ctrl+Right | Move to next word |
| Ctrl+Backspace | Delete word backward |
| Ctrl+Delete | Delete word forward |
| **Line Operations** | |
| Ctrl+D | Duplicate current line |
| Ctrl+K | Delete current line |
| Ctrl+J | Join with next line |
| Alt+Shift+Up | Move line up |
| Alt+Shift+Down | Move line down |
| **Indentation** | |
| Tab | Indent line (add spaces) |
| Shift+Tab | Unindent line |
| **Comments** | |
| Ctrl+/ | Toggle line comment |
| Ctrl+\\ | Toggle block comment |
| **Display** | |
| Alt+T | Cycle through themes |
| Alt+L | Toggle line numbers |
| Alt+Z | Toggle center/typewriter scroll |

## Mouse Support

| Action | Description |
|--------|-------------|
| Click | Position cursor |
| Click + Drag | Select text |
| Shift + Click | Extend selection |
| Double-click | Select word |
| Triple-click | Select line |
| Scroll wheel | Scroll vertically |

## Themes

Miter includes 102 color themes in the `themes/` directory. Press Alt+T to cycle through them, or edit theme files to customize colors.

Theme categories include:
- Popular themes (Dracula, Monokai, Nord, Gruvbox, Solarized, etc.)
- Light themes for daytime use
- High contrast themes for accessibility
- Colorblind-friendly themes (deuteranopia, protanopia, tritanopia variants)

## Architecture

Miter uses a simple architecture optimized for modern hardware:

- **Simple 2D array** for text storage - cache-friendly, no complex data structures
- **In-memory undo stack** - fast undo/redo without external dependencies
- **Raw terminal rendering** - ANSI escape sequences, no ncurses dependency
- **PCRE2 for syntax highlighting** - accurate regex-based highlighting

## Relationship to Terra

Miter was forked from [Terra](https://github.com/deths74r/terra), removing the SQLite/SQL processing functionality to create a focused, lightweight editor. Terra continues as a CLI text processing tool with SQL queries.

## Contributing

Contributions welcome! Please see [docs/CODING_STANDARDS.md](docs/CODING_STANDARDS.md) for code style guidelines.

## License

GPL-2.0-only
