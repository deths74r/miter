/*
 * Miter - A lightweight terminal text editor.
 * Copyright (C) 2025 Edward J Edmonds (deths74r) <edwardedmonds@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * -----------------------------------------------------------------------
 * Based on Kilo by Salvatore Sanfilippo (antirez).
 * Copyright (c) 2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.
 */

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>

/* PCRE2 for regex-based syntax highlighting patterns */
#ifndef PCRE2_DISABLED
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

/*** defines ***/

/* Editor version string displayed in welcome message */
#define MITER_VERSION "0.0.1"
/* Number of spaces to render for each tab character */
#define MITER_TAB_STOP 8
/* Number of Ctrl-Q presses required to quit with unsaved changes */
#define MITER_QUIT_TIMES 3

/* Buffer size for reading cursor position response from terminal */
#define CURSOR_POSITION_BUFFER_SIZE 32
/* Buffer size for formatting line number display in gutter */
#define LINE_NUMBER_BUFFER_SIZE 16
/* Buffer size for reading lines from config files */
#define CONFIG_LINE_BUFFER_SIZE 256
/* Initial size for prompt input buffer (grows dynamically) */
#define PROMPT_INITIAL_BUFFER_SIZE 128
/* Buffer size for formatting RGB color escape sequences */
#define COLOR_ESCAPE_BUFFER_SIZE 32
/* Buffer size for status message text */
#define STATUS_MESSAGE_BUFFER_SIZE 128
/* Buffer size for status bar left and right sections */
#define STATUS_BAR_BUFFER_SIZE 80
/* Buffer size for welcome message text */
#define WELCOME_BUFFER_SIZE 80
/* Buffer size for reading escape sequence characters */
#define ESCAPE_SEQ_BUFFER_SIZE 3

/* Unix file permission mode for newly created files (rw-r--r--) */
#define FILE_PERMISSION_DEFAULT 0644

/* Duration in seconds before status messages fade */
#define STATUS_MESSAGE_TIMEOUT_SECONDS 5

/* Undo system configuration */
#define UNDO_MEMORY_GROUPS_MAX 100   /* Max groups to keep in memory */
#define UNDO_GROUP_TIMEOUT_MS 500    /* Idle time to start new undo group */

/* Initial capacity for search results array */
#define INITIAL_SEARCH_RESULT_CAPACITY 16

/* Divisor for vertical centering of welcome message */
#define WELCOME_MESSAGE_ROW_DIVISOR 3
/* Number of rows reserved for status and message bars */
#define SCREEN_RESERVED_ROWS 2
/* Default column width for hard wrap (Alt-Q) */
#define DEFAULT_WRAP_COLUMN 80
/* Characters to search backward for word boundary during wrapping */
#define WORD_BREAK_SEARCH_WINDOW 20

/* Timeout for terminal read in 1/10 second units */
#define VTIME_DECISECONDS 1
/* Bitmask for converting key to Ctrl+key equivalent */
#define CTRL_KEY_MASK 0x1f
/* Upper bound for 7-bit ASCII character values */
#define ASCII_MAX 128

/* Starting value for special key codes to avoid ASCII conflicts */
#define EDITOR_KEY_SPECIAL_BASE 1000

/* ANSI escape: clear entire screen */
#define ESCAPE_CLEAR_SCREEN "\x1b[2J"
#define ESCAPE_CLEAR_SCREEN_LEN 4
/* ANSI escape: move cursor to position 1,1 (home) */
#define ESCAPE_CURSOR_HOME "\x1b[H"
#define ESCAPE_CURSOR_HOME_LEN 3
/* ANSI escape: hide cursor */
#define ESCAPE_HIDE_CURSOR "\x1b[?25l"
#define ESCAPE_HIDE_CURSOR_LEN 6
/* ANSI escape: show cursor */
#define ESCAPE_SHOW_CURSOR "\x1b[?25h"
#define ESCAPE_SHOW_CURSOR_LEN 6
/* ANSI escape: clear from cursor to end of line */
#define ESCAPE_CLEAR_LINE "\x1b[K"
#define ESCAPE_CLEAR_LINE_LEN 3
/* ANSI escape: reset all text attributes to default */
#define ESCAPE_RESET_ATTRIBUTES "\x1b[0m"
#define ESCAPE_RESET_ATTRIBUTES_LEN 4
/* ANSI escape: enable reverse video (swap fg/bg colors) */
#define ESCAPE_REVERSE_VIDEO "\x1b[7m"
#define ESCAPE_REVERSE_VIDEO_LEN 4
/* ANSI escape: disable all attributes (same as reset) */
#define ESCAPE_NORMAL_VIDEO "\x1b[m"
#define ESCAPE_NORMAL_VIDEO_LEN 3
/* ANSI escape: start underline */
#define ESCAPE_UNDERLINE_START "\x1b[4m"
#define ESCAPE_UNDERLINE_START_LEN 4
/* ANSI escape: end underline */
#define ESCAPE_UNDERLINE_END "\x1b[24m"
#define ESCAPE_UNDERLINE_END_LEN 5
/* ANSI escape: enable strikethrough text */
#define ESCAPE_STRIKETHROUGH_START "\x1b[9m"
#define ESCAPE_STRIKETHROUGH_START_LEN 4
/* ANSI escape: disable strikethrough text */
#define ESCAPE_STRIKETHROUGH_END "\x1b[29m"
#define ESCAPE_STRIKETHROUGH_END_LEN 5
/* ANSI escape: request cursor position report */
#define ESCAPE_GET_CURSOR_POSITION "\x1b[6n"
#define ESCAPE_GET_CURSOR_POSITION_LEN 4
/* ANSI escape: move cursor to bottom-right corner */
#define ESCAPE_MOVE_CURSOR_TO_END "\x1b[999C\x1b[999B"
#define ESCAPE_MOVE_CURSOR_TO_END_LEN 12

/* Carriage return and line feed sequence */
#define CRLF "\r\n"
#define CRLF_LEN 2

/* ANSI escape format: position cursor at row, column */
#define ESCAPE_CURSOR_POSITION_FORMAT "\x1b[%d;%dH"
/* ANSI escape format: set foreground color with RGB values */
#define ESCAPE_FOREGROUND_RGB_FORMAT "\x1b[38;2;%d;%d;%dm"
/* ANSI escape format: set background color with RGB values */
#define ESCAPE_BACKGROUND_RGB_FORMAT "\x1b[48;2;%d;%d;%dm"

/* ASCII escape character value */
#define CHAR_ESCAPE '\x1b'

/* Mouse tracking escape sequences */
#define MOUSE_ENABLE_NORMAL "\x1b[?1000h"    /* Basic mouse reporting */
#define MOUSE_ENABLE_BUTTON "\x1b[?1002h"    /* Button-event tracking (drag) */
#define MOUSE_ENABLE_SGR "\x1b[?1006h"       /* SGR extended format */
#define MOUSE_DISABLE_NORMAL "\x1b[?1000l"
#define MOUSE_DISABLE_BUTTON "\x1b[?1002l"
#define MOUSE_DISABLE_SGR "\x1b[?1006l"

/* Mouse scroll direction */
#define MOUSE_SCROLL_LINES 3

/* Mouse button codes (SGR format) */
#define MOUSE_BUTTON_LEFT 0
#define MOUSE_BUTTON_MIDDLE 1
#define MOUSE_BUTTON_RIGHT 2
#define MOUSE_SCROLL_UP 64
#define MOUSE_SCROLL_DOWN 65

/* Screen center for typewriter scrolling (cursor stays near vertical center) */
#define SCREEN_CENTER (editor.screen_rows / 2)

/* Kitty multiple cursors protocol (v0.43.0+)
 * Format: CSI > SHAPE;COORD_TYPE:Y:X SPACE q
 * Shape 29 = follow main cursor shape
 * Coord type 2 = individual cell positions (1-indexed)
 * Coord type 4 = rectangle (full screen if no coords = clear all)
 */
#define ESCAPE_KITTY_CURSOR_CLEAR "\x1b[>0;4 q"
#define ESCAPE_KITTY_CURSOR_CLEAR_LEN 8
#define ESCAPE_KITTY_CURSOR_FORMAT "\x1b[>29;2:%d:%d q"

/* Convert keypress to Ctrl+key value by masking upper bits */
#define CTRL_KEY(k) ((k) & CTRL_KEY_MASK)

/* Key codes for special keys (arrows, function keys, etc.)
 * Start at EDITOR_KEY_SPECIAL_BASE to avoid conflict with ASCII */
enum editor_key {
  BACKSPACE = 127,
  ARROW_LEFT = EDITOR_KEY_SPECIAL_BASE,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  ALT_T,
  ALT_L,
  ALT_Q,
  ALT_J,
  ALT_S,
  ALT_R,
  ALT_N,
  ALT_W,
  ALT_C,
  ALT_V,
  MOUSE_EVENT,
  SHIFT_ARROW_UP,
  SHIFT_ARROW_DOWN,
  SHIFT_ARROW_LEFT,
  SHIFT_ARROW_RIGHT,
  SHIFT_HOME,
  SHIFT_END,
  SHIFT_TAB,
  CTRL_ARROW_LEFT,
  CTRL_ARROW_RIGHT,
  CTRL_BACKSPACE,
  CTRL_DELETE,
  ALT_SHIFT_UP,
  ALT_SHIFT_DOWN,
  ALT_UP,
  ALT_DOWN,
  ALT_Z,
  ALT_OPEN_BRACKET,
  ALT_CLOSE_BRACKET,
  ALT_M,
  F10_KEY
};

/* Undo operation types for logging edits */
enum undo_op_type {
  UNDO_CHAR_INSERT = 1,       /* Single character inserted */
  UNDO_CHAR_DELETE = 2,       /* Single character deleted (backspace) */
  UNDO_CHAR_DELETE_FWD = 3,   /* Delete key (forward delete) */
  UNDO_ROW_INSERT = 4,        /* New row inserted (Enter at end of line) */
  UNDO_ROW_DELETE = 5,        /* Row deleted (backspace at start joins lines) */
  UNDO_ROW_SPLIT = 6,         /* Row split into two (Enter in middle) */
  UNDO_SELECTION_DELETE = 7,  /* Selection deleted */
  UNDO_PASTE = 8              /* Text pasted (multi-char/line) */
};

/* In-memory undo entry */
typedef struct undo_entry {
  int group_id;
  enum undo_op_type op_type;
  int cursor_row;
  int cursor_col;
  int row_idx;
  char *row_content;          /* For row insert/delete */
  int char_pos;
  char *char_data;            /* For char insert/delete */
  int end_row;
  int end_col;
  char *multi_line;           /* For selection/paste */
} undo_entry;

#define UNDO_MAX_ENTRIES 10000

/* Mouse modifier bits in SGR format */
#define MOUSE_MOD_SHIFT 4   /* bit 2 */
#define MOUSE_MOD_ALT   8   /* bit 3 */
#define MOUSE_MOD_CTRL  16  /* bit 4 */
#define MOUSE_MOTION    32  /* bit 5: motion event while button held */

/* Mouse event data from terminal */
typedef struct {
  int button;      /* Raw button field including modifiers */
  int button_base; /* Just the button (0-2) without modifiers */
  int modifiers;   /* Extracted Shift/Alt/Ctrl bits */
  int column;      /* Screen column (1-indexed from terminal) */
  int row;         /* Screen row (1-indexed from terminal) */
  int is_release;  /* 1 if button release, 0 if press */
  int is_motion;   /* 1 if motion event while dragging */
} mouse_event;

/* Selection position in file coordinates */
typedef struct {
  int row;  /* Line number (0-indexed) */
  int col;  /* Character position in chars[] */
} selection_pos;

/* Selection modes */
enum selection_mode {
  SELECTION_NONE = 0,
  SELECTION_CHAR = 1,  /* Character selection */
  SELECTION_WORD = 2,  /* Word selection (double-click) */
  SELECTION_LINE = 3   /* Line selection (triple-click) */
};

/* Selection state */
typedef struct {
  int active;                   /* 1 if selection exists */
  selection_pos anchor;         /* Where selection started */
  selection_pos cursor;         /* Current end (follows cursor) */
  enum selection_mode mode;     /* Type of selection */
  struct timespec last_click_time; /* For multi-click detection (ms accuracy) */
  selection_pos last_click_pos; /* Position of last click */
  int click_count;              /* 1=single, 2=double, 3=triple */
} selection_state;

/* Secondary cursor position for multi-cursor editing.
 * The primary cursor uses editor.cursor_x/cursor_y directly.
 * Secondary cursors are stored in an array of these structs. */
typedef struct {
  int line;                     /* Row in file (0-indexed) */
  int column;                   /* Column in file (0-indexed, in chars[]) */
  int has_selection;            /* 1 if this cursor has an active selection */
  int anchor_line;              /* Selection anchor row */
  int anchor_column;            /* Selection anchor column */
} cursor_position;

/* Syntax highlighting categories for coloring text */
enum editor_highlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH,
  HL_BRACKET_MATCH
};

/* Syntax highlighting feature flags (bitmask) */
/* Enable highlighting of numeric literals */
#define HL_HIGHLIGHT_NUMBERS (1<<0)
/* Enable highlighting of string literals */
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

/*
 * Syntax highlighting configuration for a programming language.
 * Defines patterns for identifying different syntactic elements like
 * comments, strings, and keywords to enable colored highlighting.
 */
struct editor_syntax {
  /* Display name shown in status bar (e.g., "c", "python") */
  char *filetype;
  /* NULL-terminated array of filename patterns (e.g., ".c", ".h") */
  char **filematch;
  /* NULL-terminated array of keywords; trailing '|' marks type 2 keywords */
  char **keywords;
  /* String that starts a single-line comment (e.g., "//") */
  char *singleline_comment_start;
  /* String that starts a multi-line comment (e.g., slash-star) */
  char *multiline_comment_start;
  /* String that ends a multi-line comment (e.g., star-slash) */
  char *multiline_comment_end;
  /* Bitmask of HL_HIGHLIGHT_* flags */
  int flags;
};

#ifndef PCRE2_DISABLED
/*
 * Compiled PCRE2 pattern for regex-based syntax highlighting.
 * Patterns are JIT-compiled for performance and cached per language.
 */
typedef struct syntax_pattern {
  pcre2_code *regex;              /* Compiled PCRE2 pattern */
  pcre2_match_data *match_data;   /* Reusable match data */
  int highlight_type;             /* HL_* type to apply on match */
  int priority;                   /* Higher priority patterns checked first */
} syntax_pattern;
#endif

/*
 * Represents a single line of text in the editor buffer.
 * Maintains both raw and rendered versions for tab expansion and
 * syntax highlighting, plus soft wrap break positions.
 */
typedef struct editor_row {
  /* Index of this row in the editor's row array */
  int line_index;
  /* Number of characters in chars (excluding null terminator) */
  int line_size;
  /* Number of characters in render (excluding null terminator) */
  int render_size;
  /* Raw line content as typed by user */
  char *chars;
  /* Rendered content with tabs expanded to spaces */
  char *render;
  /* Syntax highlighting type for each character in render */
  unsigned char *highlight;
  /* True if this row ends inside a multi-line comment */
  int open_comment;
  /* True if row has been modified since last SQLite sync */
  int dirty;
  /* Array of render positions where soft wrap breaks occur */
  int *wrap_breaks;
  /* Number of wrap break positions in wrap_breaks array */
  int wrap_break_count;
} editor_row;

/*
 * Stores location of a single search match found by FTS5.
 * Used to cache search results for navigation with F3/Shift-F3.
 */
typedef struct {
  /* 1-based line number where match was found */
  int line_number;
  /* Character offset within the line where match starts */
  int match_offset;
  /* Number of characters in the matched text */
  int match_length;
} search_result;

/*** theming ***/

/*
 * RGB color value for 24-bit true color terminal output.
 * Each component ranges from 0-255.
 */
typedef struct {
  /* Red component (0-255) */
  unsigned char r;
  /* Green component (0-255) */
  unsigned char g;
  /* Blue component (0-255) */
  unsigned char b;
} rgb_color;

/* Generate theme color enum from X-macro */
enum theme_color {
#define X(name, r, g, b, desc) THEME_##name,
#include "themes/monochrome_dark.def"
#undef X
  THEME_COLOR_COUNT
};

/* Active theme colors (modifiable at runtime) */
static rgb_color active_theme[THEME_COLOR_COUNT];

/*
 * Runtime theme storage with heap-allocated name.
 * Themes are loaded dynamically from .def files at startup.
 */
typedef struct {
  /* Heap-allocated theme name (must be freed) */
  char *name;
  /* Array of colors indexed by theme_color enum values */
  rgb_color colors[THEME_COLOR_COUNT];
} runtime_theme;

/* Dynamic theme registry - grows as themes are loaded */
static runtime_theme *loaded_themes = NULL;
static int loaded_theme_count = 0;
static int loaded_theme_capacity = 0;

/* Fallback theme colors (monochrome) if no themes can be loaded */
static const rgb_color fallback_theme_colors[THEME_COLOR_COUNT] = {
#define X(name, r, g, b, desc) {r, g, b},
#include "themes/monochrome_dark.def"
#undef X
};

/* Color name lookup table for parsing .def files */
static const char *theme_color_names[] = {
#define X(name, r, g, b, desc) #name,
#include "themes/monochrome_dark.def"
#undef X
};

/* Access loaded themes like an array for compatibility */
#define THEME_COUNT loaded_theme_count

/* Store last parsed mouse event for handler to read */
static mouse_event last_mouse_event;

/*
 * Global editor state containing all runtime configuration and data.
 * Single instance 'editor' holds cursor position, file content, display
 * settings, SQLite connection, and search state.
 */
struct editor_config {
  /* Cursor position in file coordinates (0-indexed) */
  int cursor_x, cursor_y;
  /* Rendered x position accounting for tabs */
  int render_x;
  /* First visible row (for vertical scrolling) */
  int row_offset;
  /* First visible column (for horizontal scrolling) */
  int column_offset;
  /* Number of rows available for text (excludes status bars) */
  int screen_rows;
  /* Terminal width in characters */
  int screen_columns;
  /* Number of lines in the file */
  int row_count;
  /* Array of editor_row structs for each line */
  editor_row *row;
  /* True if file has unsaved modifications */
  int dirty;
  /* Path to current file, NULL if unnamed */
  char *filename;
  /* Status bar message text */
  char status_message[STATUS_MESSAGE_BUFFER_SIZE];
  /* Time when status message was set (for timeout) */
  time_t status_message_time;
  /* Current syntax highlighting rules, NULL if none */
  struct editor_syntax *syntax;
#ifndef PCRE2_DISABLED
  /* Array of compiled PCRE2 patterns for current language */
  syntax_pattern *syntax_patterns;
  /* Number of patterns in syntax_patterns array */
  int syntax_pattern_count;
#endif
  /* Original terminal settings to restore on exit */
  struct termios original_termios;
  /* Array of cached search results */
  search_result *search_results;
  /* Number of results in search_results array */
  int search_result_count;
  /* Allocated capacity of search_results array */
  int search_result_capacity;
  /* Index into themes array for current color theme */
  int current_theme_index;
  /* True if line numbers gutter is visible */
  int show_line_numbers;
  /* Width of line number gutter in characters */
  int gutter_width;
  /* Column position for hard wrap (Alt-Q) */
  int wrap_column;
  /* True if soft wrap (visual wrapping) is enabled */
  int soft_wrap;
  /* True if center/typewriter scrolling is enabled (cursor stays near center) */
  int center_scroll;
  /* Tactile scroll state - velocity-based scroll acceleration */
  struct timespec last_scroll_time;  /* Timestamp of last mouse scroll */
  int scroll_speed;                   /* Current scroll multiplier (1-15) */
  /* Selection state for text selection */
  selection_state selection;
  /* Last synced system clipboard content (for smart merge) */
  char *last_system_clipboard;
  /* Undo system state */
  int undo_group_id;        /* Current undo group number */
  int undo_position;        /* Current position in undo stack (for redo) */
  int undo_memory_groups;   /* Count of groups currently in memory */
  int undo_logging;         /* 0 = normal, 1 = during undo/redo (skip logging) */
  undo_entry *undo_stack;   /* In-memory undo stack */
  int undo_stack_count;     /* Number of entries in undo stack */
  int undo_stack_capacity;  /* Allocated capacity */
  struct timespec last_edit_time; /* For undo grouping */
  /* Bracket matching state */
  int bracket_match_row;    /* Row of matching bracket (-1 if none) */
  int bracket_match_col;    /* Column of matching bracket */
  int bracket_open_row;     /* Row of opening delimiter (-1 if none) */
  int bracket_open_col;     /* Column of opening delimiter */
  int bracket_open_len;     /* Length of opening delimiter (1 for single char, 2 for comment) */
  int bracket_close_row;    /* Row of closing delimiter (-1 if none) */
  int bracket_close_col;    /* Column of closing delimiter */
  int bracket_close_len;    /* Length of closing delimiter */
  /* Smart Home key state - tracks if last key was Home for toggle behavior */
  int last_key_was_home;
  /* Multi-cursor editing state */
  cursor_position *cursors;     /* Array of secondary cursor positions */
  size_t cursor_count;          /* Number of secondary cursors */
  size_t cursor_capacity;       /* Allocated capacity of cursors array */
  int cursors_follow_primary;   /* 1 = secondary cursors follow movement */
  int allow_primary_overlap;    /* 1 = keep secondary cursor at primary position */
  /* Menu bar state */
  int menu_bar_visible;         /* 1 = show menu bar (default), 0 = hide */
  int menu_open;                /* -1 = closed, 0+ = index of open menu */
  int menu_selected_item;       /* Selected item within open dropdown */
};

struct editor_config editor;

/*** Menu bar definitions ***/

/* Forward declarations for menu actions */
void editor_open_file_browser(void);
void editor_save(void);
void editor_find(void);
void simple_search(const char *query);
void editor_undo(void);
void editor_redo(void);
void editor_copy(void);
void editor_cut(void);
void editor_paste(void);
void selection_select_all(void);
void editor_toggle_line_numbers(void);
void editor_toggle_soft_wrap(void);
void editor_set_status_message(const char *fmt, ...);
void editor_free_row(editor_row *row);
void theme_cycle(void);

/* Menu item structure */
typedef struct {
  const char *label;      /* Display text (NULL for separator) */
  const char *shortcut;   /* Keyboard shortcut display, e.g., "Ctrl+S" */
  void (*action)(void);   /* Function to call when selected */
} menu_item;

/* Menu definition structure */
typedef struct {
  const char *title;      /* Menu title: "File", "Edit", etc. */
  menu_item *items;       /* Array of menu items */
  int item_count;         /* Number of items */
  int x_position;         /* Calculated screen position (set during render) */
  int width;              /* Calculated dropdown width (set during render) */
} menu_def;

/* Menu action wrapper functions (to handle quit specially) */
static int menu_quit_requested = 0;

static void menu_action_new(void) {
  /* Clear buffer for new file */
  if (editor.dirty) {
    editor_set_status_message("Save changes first (Ctrl+S) or quit without saving (Ctrl+Q 3x)");
    return;
  }
  for (int i = 0; i < editor.row_count; i++) {
    editor_free_row(&editor.row[i]);
  }
  free(editor.row);
  editor.row = NULL;
  editor.row_count = 0;
  free(editor.filename);
  editor.filename = NULL;
  editor.dirty = 0;
  editor.cursor_x = editor.cursor_y = 0;
  editor.row_offset = editor.column_offset = 0;
  editor_set_status_message("New file");
}

static void menu_action_quit(void) {
  menu_quit_requested = 1;
}

static void menu_action_about(void) {
  editor_set_status_message("Terra - SQLite-powered terminal text editor | github.com/deths74r/terra");
}

/* Menu item definitions */
static menu_item file_menu_items[] = {
  {"New",        "Ctrl+N", menu_action_new},
  {"Open...",    "Ctrl+O", editor_open_file_browser},
  {"Save",       "Ctrl+S", editor_save},
  {NULL,         NULL,     NULL},  /* Separator */
  {"Quit",       "Ctrl+Q", menu_action_quit}
};

static menu_item edit_menu_items[] = {
  {"Undo",       "Ctrl+Z", editor_undo},
  {"Redo",       "Ctrl+Y", editor_redo},
  {NULL,         NULL,     NULL},  /* Separator */
  {"Cut",        "Ctrl+X", editor_cut},
  {"Copy",       "Ctrl+C", editor_copy},
  {"Paste",      "Ctrl+V", editor_paste},
  {NULL,         NULL,     NULL},
  {"Select All", "Ctrl+A", selection_select_all},
  {"Find...",    "Ctrl+F", editor_find}
};

static menu_item view_menu_items[] = {
  {"Line Numbers", "Alt+L", editor_toggle_line_numbers},
  {"Soft Wrap",    "Alt+W", editor_toggle_soft_wrap},
  {NULL,           NULL,    NULL},
  {"Next Theme",   "Alt+T", theme_cycle}
};

static menu_item help_menu_items[] = {
  {"About Terra", NULL, menu_action_about}
};

static menu_def menus[] = {
  /* title, items, item_count, x_position, width */
  /* x_positions: " File " = 0, " Edit " = 6, " View " = 12, " Help " = 18 */
  {"File", file_menu_items, 5, 0, 0},
  {"Edit", edit_menu_items, 9, 6, 0},
  {"View", view_menu_items, 4, 12, 0},
  {"Help", help_menu_items, 1, 18, 0}
};
#define MENU_COUNT 4

/* Flag for SIGWINCH signal (terminal resize) */
volatile sig_atomic_t window_resize_pending = 0;

/* Signal handler for SIGWINCH (terminal resize).
 * Sets a flag to be handled in the main loop. */
void handle_sigwinch(int sig) {
  (void)sig;
  window_resize_pending = 1;
}

/*** filetypes ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editor_syntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};

/* Number of language definitions in the syntax highlight database */
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editor_set_status_message(const char *fmt, ...);
void editor_refresh_screen();
char *editor_prompt(char *prompt, void (*callback)(char *, int));
#ifndef PCRE2_DISABLED
void syntax_free_patterns();
#endif
void theme_init();
void theme_load(int index);
void theme_cycle();
const char* theme_get_name();
void theme_save();
int theme_load_name_from_config(char *name_buf, int buf_size);
int theme_find_by_name(const char *name);
void theme_registry_add(const char *name, rgb_color *colors);
int theme_load_from_file(const char *filepath);
void theme_load_directory(const char *dir);
void theme_discover_all();
int theme_color_name_to_index(const char *name);
void theme_registry_free();
void editor_update_gutter_width();
void editor_toggle_line_numbers();
void editor_toggle_soft_wrap();
void editor_toggle_center_scroll();
void editor_update_scroll_speed();
void editor_calculate_wrap_breaks(editor_row *row, int available_width);
rgb_color theme_get_color(enum theme_color color_id);
int rgb_equal(rgb_color color_a, rgb_color color_b);
void editor_handle_mouse_event();
int editor_find_matching_bracket();
void editor_jump_to_matching_bracket();
void editor_reset_bracket_match();
int parse_sgr_mouse_event();
void undo_start_new_group();
void undo_maybe_start_group(int force_new);
void undo_log(enum undo_op_type type, int cursor_row, int cursor_col,
              int row_idx, int char_pos, const char *char_data,
              int end_row, int end_col, const char *multi_line);
void undo_clear_redo();
void editor_undo();
void editor_redo();
void editor_handle_resize();
void editor_insert_char(int character);
void editor_insert_newline();
int clipboard_store(const char *content, int content_type);
char *clipboard_get_latest(int *content_type);
void clipboard_sync_to_system(const char *content);
char *clipboard_read_from_system();
void clipboard_smart_merge();
int is_word_char(int c);
int get_first_nonwhitespace_col(editor_row *row);
int editor_line_indentation(editor_row *row);
int editor_line_ends_with_opening_brace(editor_row *row);
int editor_auto_unindent_closing_brace(int line);
/* Forward declarations for multi-cursor helpers used early */
cursor_position *multicursor_collect_all(size_t *count, int reverse);
void multicursor_remove_duplicates();
static bool *multicursor_mark_primary(cursor_position *all, size_t total);
void editor_row_append_string(editor_row *row, char *s, size_t len);

/*** word wrapping utilities ***/

/* Check if character is whitespace (space, tab, newline, or carriage return). */
int character_is_whitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

/* Calculate how many screen rows a line needs when soft-wrapped
 * Returns 1 if soft wrap is disabled or line fits in one row
 */
int editor_row_visual_rows(editor_row *row) {
  if (!editor.soft_wrap) return 1;

  int available_width = editor.screen_columns - editor.gutter_width;
  if (available_width <= 0) return 1;

  if (row->render_size <= available_width) return 1;

  /* Calculate wrap breaks if not already done */
  editor_calculate_wrap_breaks(row, available_width);

  /* Number of visual rows = number of breaks + 1 */
  return row->wrap_break_count + 1;
}

/* Get the start position (in render buffer) for a specific wrap segment
 * segment 0 is the first line, segment 1 is second, etc. */
int editor_wrap_segment_start(editor_row *row, int segment) {
  if (segment == 0) return 0;
  if (segment > row->wrap_break_count) return row->render_size;
  return row->wrap_breaks[segment - 1];
}

/* Get the end position (in render buffer) for a specific wrap segment */
int editor_wrap_segment_end(editor_row *row, int segment) {
  if (segment >= row->wrap_break_count) return row->render_size;
  return row->wrap_breaks[segment];
}

/* Calculate which wrap segment a given render position falls in */
int editor_rx_to_wrap_segment(editor_row *row, int rx) {
  if (row->wrap_break_count == 0) return 0;

  for (int i = 0; i < row->wrap_break_count; i++) {
    if (rx < row->wrap_breaks[i]) return i;
  }
  /* Last segment */
  return row->wrap_break_count;
}

/* Count total visual rows from row 0 to row (inclusive)
 * This tells us the visual screen row where this logical row starts
 */
int editor_visual_rows_up_to(int row) {
  if (!editor.soft_wrap || row < 0) return row < 0 ? 0 : row;

  int visual = 0;
  for (int i = 0; i <= row && i < editor.row_count; i++) {
    visual += editor_row_visual_rows(&editor.row[i]);
  }
  return visual;
}

/* Given cursor position (cx, cy), calculate which wrap row within the line
 * Returns 0 for first wrap row, 1 for second, etc.
 */
int editor_cursor_wrap_row() {
  if (!editor.soft_wrap || editor.cursor_y >= editor.row_count) return 0;

  int available_width = editor.screen_columns - editor.gutter_width;
  if (available_width <= 0) return 0;

  editor_row *row = &editor.row[editor.cursor_y];
  editor_calculate_wrap_breaks(row, available_width);

  return editor_rx_to_wrap_segment(row, editor.render_x);
}

/* Map visual row to logical row and wrap segment
 * visual_row: screen row (accounting for wrapped lines)
 * *logical_row: output - which file row
 * *wrap_row: output - which wrap segment within that row (0 = first, 1 = second, etc.)
 * Returns 1 if valid, 0 if past end of file
 */
int editor_visual_to_logical(int visual_row, int *logical_row, int *wrap_row) {
  if (!editor.soft_wrap) {
    *logical_row = visual_row;
    *wrap_row = 0;
    return visual_row < editor.row_count;
  }

  int visual = 0;
  for (int i = 0; i < editor.row_count; i++) {
    int rows_for_line = editor_row_visual_rows(&editor.row[i]);
    if (visual + rows_for_line > visual_row) {
      /* This is the line */
      *logical_row = i;
      *wrap_row = visual_row - visual;
      return 1;
    }
    visual += rows_for_line;
  }

  /* Past end of file */
  *logical_row = editor.row_count;
  *wrap_row = 0;
  return 0;
}

/* Find the best wrap point before max_col in the given row
 * Returns the column position where we should wrap, or max_col if no good point found
 * Searches backward from max_col for whitespace (word boundary)
 */
int find_wrap_point(editor_row *row, int max_col) {
  /* No wrap needed */
  if (row->render_size <= max_col) {
    return row->render_size;
  }

  /* Search backward from max_col for whitespace */
  for (int i = max_col; i > max_col - WORD_BREAK_SEARCH_WINDOW && i > 0; i--) {
    /* Wrap at this whitespace */
    if (character_is_whitespace(row->render[i])) {
      return i;
    }
  }

  /* No whitespace found within search window - hard break at max_col */
  return max_col;
}

/*
 * Defines a contiguous range of lines in the file buffer.
 * Used by hard wrap (Alt-Q) to identify paragraphs for reflowing.
 */
typedef struct {
  /* First line index of the paragraph (inclusive) */
  int start_line;
  /* Last line index of the paragraph (inclusive) */
  int end_line;
} paragraph_range;

/* Detect paragraph boundaries around the cursor line
 * A paragraph is a sequence of non-blank lines
 * Blank lines separate paragraphs
 */
paragraph_range detect_paragraph(int cursor_line) {
  paragraph_range range;
  range.start_line = cursor_line;
  range.end_line = cursor_line;

  /* Find start: scan upward until blank line or start of file */
  for (int line = cursor_line; line > 0; line--) {
    if (editor.row[line].line_size == 0) {
      range.start_line = line + 1;
      break;
    }
    range.start_line = line;
    if (line == 0) break;
  }

  /* Find end: scan downward until blank line or end of file */
  for (int line = cursor_line; line < editor.row_count; line++) {
    if (editor.row[line].line_size == 0) {
      range.end_line = line - 1;
      break;
    }
    range.end_line = line;
  }

  return range;
}

/*
 * Stores detected indentation and comment marker prefix from a line.
 * Used during hard wrap to preserve formatting when reflowing paragraphs.
 * The prefix is prepended to each wrapped line.
 */
typedef struct {
  /* Allocated string containing spaces/tabs and comment markers */
  char *prefix;
  /* Length of prefix string in characters */
  int length;
} line_prefix;

/* Detect and extract line prefix (indentation + optional comment markers)
 * Returns allocated prefix string that must be freed by caller
 */
line_prefix detect_line_prefix(editor_row *row) {
  line_prefix result;
  result.prefix = NULL;
  result.length = 0;

  if (row->line_size == 0) {
    return result;
  }

  int i = 0;

  /* Count leading whitespace */
  while (i < row->line_size && (row->chars[i] == ' ' || row->chars[i] == '\t')) {
    i++;
  }

  /* Check for comment markers (// or *) */
  if (i < row->line_size - 1 && row->chars[i] == '/' && row->chars[i+1] == '/') {
    i += 2;
    /* Skip trailing space after // */
    if (i < row->line_size && row->chars[i] == ' ') {
      i++;
    }
  } else if (i < row->line_size && row->chars[i] == '*') {
    i++;
    if (i < row->line_size && row->chars[i] == ' ') {
      i++;
    }
  }

  if (i > 0) {
    result.prefix = malloc(i + 1);
    memcpy(result.prefix, row->chars, i);
    result.prefix[i] = '\0';
    result.length = i;
  }

  return result;
}

/*** terminal ***/

/* Print error message and exit. Clears screen first. */
void die(const char *message) {
  write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN, ESCAPE_CLEAR_SCREEN_LEN);
  write(STDOUT_FILENO, ESCAPE_CURSOR_HOME, ESCAPE_CURSOR_HOME_LEN);

  perror(message);
  exit(1);
}

/* Restore terminal to canonical mode. Called via atexit(). */
void disable_raw_mode() {
  /* Disable mouse tracking before restoring terminal */
  write(STDOUT_FILENO, MOUSE_DISABLE_SGR, 8);
  write(STDOUT_FILENO, MOUSE_DISABLE_BUTTON, 8);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.original_termios) == -1)
    die("tcsetattr");
}

/* Put terminal into raw mode for character-by-character input. */
void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &editor.original_termios) == -1) die("tcgetattr");
  atexit(disable_raw_mode);

  struct termios raw = editor.original_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = VTIME_DECISECONDS;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

  /* Enable mouse tracking (SGR extended mode for large terminals) */
  /* Mode 1006: SGR format for coordinates (supports large terminals) */
  /* Mode 1002: Button-event tracking (reports motion while button held) */
  /* NOTE: Do NOT enable Mode 1000 - it conflicts with and disables Mode 1002 */
  write(STDOUT_FILENO, MOUSE_ENABLE_SGR, 8);
  write(STDOUT_FILENO, MOUSE_ENABLE_BUTTON, 8);
}

/*
 * Parse SGR extended mouse format: ESC [ < Pb ; Px ; Py M/m
 * Populates last_mouse_event and returns MOUSE_EVENT on success.
 */
int parse_sgr_mouse_event() {
  char buffer[32];
  int index = 0;
  char character;

  /* Read until 'M' (press) or 'm' (release) */
  while (index < 31) {
    if (read(STDIN_FILENO, &character, 1) != 1) return CHAR_ESCAPE;
    buffer[index++] = character;
    if (character == 'M' || character == 'm') break;
  }
  buffer[index] = '\0';

  /* Parse: button;column;row */
  int button, column, row;
  if (sscanf(buffer, "%d;%d;%d", &button, &column, &row) != 3) {
    return CHAR_ESCAPE;
  }

  last_mouse_event.button = button;
  last_mouse_event.is_motion = (button & MOUSE_MOTION) ? 1 : 0;

  /* Strip motion bit before processing button */
  int btn = button & ~MOUSE_MOTION;

  /* For scroll wheel (button >= 64), preserve full value; otherwise extract 0-2 */
  if (btn >= 64) {
    last_mouse_event.button_base = btn;  /* Scroll events: keep 64/65 */
  } else {
    last_mouse_event.button_base = btn & 3;  /* Regular buttons: 0, 1, 2 */
  }
  last_mouse_event.modifiers = btn & (MOUSE_MOD_SHIFT | MOUSE_MOD_ALT | MOUSE_MOD_CTRL);
  last_mouse_event.column = column;
  last_mouse_event.row = row;
  last_mouse_event.is_release = (character == 'm');

  return MOUSE_EVENT;
}

/*
 * Read a single keypress and return its key code.
 * Handles escape sequences for special keys (arrows, function keys).
 * Returns -1 if no input available (timeout).
 */
int editor_read_key() {
  int bytes_read;
  char character;

  /* Read with timeout - return -1 if no input available */
  bytes_read = read(STDIN_FILENO, &character, 1);
  /* Timeout (VTIME expired) */
  if (bytes_read == 0) return -1;
  if (bytes_read == -1) {
    /* Would block - no input */
    if (errno == EAGAIN) return -1;
    /* Actual error */
    die("read");
  }

  if (character == CHAR_ESCAPE) {
    char escape_sequence[ESCAPE_SEQ_BUFFER_SIZE];

    if (read(STDIN_FILENO, &escape_sequence[0], 1) != 1) return CHAR_ESCAPE;

    /* Handle Alt+key combinations (ESC followed by a character) */
    if (escape_sequence[0] == 't' || escape_sequence[0] == 'T') {
      return ALT_T;
    }
    if (escape_sequence[0] == 'l' || escape_sequence[0] == 'L') {
      return ALT_L;
    }
    if (escape_sequence[0] == 'q' || escape_sequence[0] == 'Q') {
      return ALT_Q;
    }
    if (escape_sequence[0] == 'j' || escape_sequence[0] == 'J') {
      return ALT_J;
    }
    if (escape_sequence[0] == 's' || escape_sequence[0] == 'S') {
      return ALT_S;
    }
    if (escape_sequence[0] == 'r' || escape_sequence[0] == 'R') {
      return ALT_R;
    }
    if (escape_sequence[0] == 'n' || escape_sequence[0] == 'N') {
      return ALT_N;
    }
    if (escape_sequence[0] == 'w' || escape_sequence[0] == 'W') {
      return ALT_W;
    }
    if (escape_sequence[0] == 'c' || escape_sequence[0] == 'C') {
      return ALT_C;
    }
    if (escape_sequence[0] == 'v' || escape_sequence[0] == 'V') {
      return ALT_V;
    }
    if (escape_sequence[0] == 'z' || escape_sequence[0] == 'Z') {
      return ALT_Z;
    }
    if (escape_sequence[0] == 'm' || escape_sequence[0] == 'M') {
      return ALT_M;
    }
    if (escape_sequence[0] == ']') {
      return ALT_CLOSE_BRACKET;
    }

    if (read(STDIN_FILENO, &escape_sequence[1], 1) != 1) {
      /* Timeout after ESC+char - check for Alt+[ */
      if (escape_sequence[0] == '[') return ALT_OPEN_BRACKET;
      return CHAR_ESCAPE;
    }

    if (escape_sequence[0] == '[') {
      /* SGR mouse format: ESC [ < ... */
      if (escape_sequence[1] == '<') {
        return parse_sgr_mouse_event();
      }
      if (escape_sequence[1] >= '0' && escape_sequence[1] <= '9') {
        if (read(STDIN_FILENO, &escape_sequence[2], 1) != 1) return CHAR_ESCAPE;
        if (escape_sequence[2] == '~') {
          switch (escape_sequence[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        } else if (escape_sequence[1] == '2' && escape_sequence[2] == '1') {
          /* F10: ESC [ 2 1 ~ */
          char seq3;
          if (read(STDIN_FILENO, &seq3, 1) != 1) return CHAR_ESCAPE;
          if (seq3 == '~') return F10_KEY;
        } else if (escape_sequence[1] == '3' && escape_sequence[2] == ';') {
          /* Ctrl+Delete: ESC [ 3 ; 5 ~ */
          char seq3, seq4;
          if (read(STDIN_FILENO, &seq3, 1) != 1) return CHAR_ESCAPE;
          if (read(STDIN_FILENO, &seq4, 1) != 1) return CHAR_ESCAPE;
          if (seq3 == '5' && seq4 == '~') return CTRL_DELETE;
        } else if (escape_sequence[1] == '1' && escape_sequence[2] == ';') {
          /* Modifier format: ESC [ 1 ; modifier key */
          char seq3, seq4;
          if (read(STDIN_FILENO, &seq3, 1) != 1) return CHAR_ESCAPE;
          if (read(STDIN_FILENO, &seq4, 1) != 1) return CHAR_ESCAPE;
          if (seq3 == '2') {  /* Shift modifier */
            switch (seq4) {
              case 'A': return SHIFT_ARROW_UP;
              case 'B': return SHIFT_ARROW_DOWN;
              case 'C': return SHIFT_ARROW_RIGHT;
              case 'D': return SHIFT_ARROW_LEFT;
              case 'H': return SHIFT_HOME;
              case 'F': return SHIFT_END;
            }
          } else if (seq3 == '3') {  /* Alt modifier */
            switch (seq4) {
              case 'A': return ALT_UP;
              case 'B': return ALT_DOWN;
            }
          } else if (seq3 == '4') {  /* Alt+Shift modifier */
            switch (seq4) {
              case 'A': return ALT_SHIFT_UP;
              case 'B': return ALT_SHIFT_DOWN;
            }
          } else if (seq3 == '5') {  /* Ctrl modifier */
            switch (seq4) {
              case 'C': return CTRL_ARROW_RIGHT;
              case 'D': return CTRL_ARROW_LEFT;
            }
          }
        }
      } else {
        switch (escape_sequence[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
          case 'Z': return SHIFT_TAB;  /* Shift+Tab: ESC [ Z */
        }
      }
    } else if (escape_sequence[0] == 'O') {
      switch (escape_sequence[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return CHAR_ESCAPE;
  } else {
    return character;
  }
}

/* Query terminal for current cursor position using escape sequence.
 * Returns 0 on success, -1 on failure. */
int cursor_get_position(int *rows, int *cols) {
  char buffer[CURSOR_POSITION_BUFFER_SIZE];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, ESCAPE_GET_CURSOR_POSITION, ESCAPE_GET_CURSOR_POSITION_LEN) != ESCAPE_GET_CURSOR_POSITION_LEN) return -1;

  while (i < sizeof(buffer) - 1) {
    if (read(STDIN_FILENO, &buffer[i], 1) != 1) break;
    if (buffer[i] == 'R') break;
    i++;
  }
  buffer[i] = '\0';

  if (buffer[0] != CHAR_ESCAPE || buffer[1] != '[') return -1;
  if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

/* Get terminal window dimensions using ioctl or cursor position fallback.
 * Returns 0 on success, -1 on failure. */
int window_get_size(int *rows, int *cols) {
  struct winsize window_size;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1 || window_size.ws_col == 0) {
    if (write(STDOUT_FILENO, ESCAPE_MOVE_CURSOR_TO_END, ESCAPE_MOVE_CURSOR_TO_END_LEN) != ESCAPE_MOVE_CURSOR_TO_END_LEN) return -1;
    return cursor_get_position(rows, cols);
  } else {
    *cols = window_size.ws_col;
    *rows = window_size.ws_row;
    return 0;
  }
}

/* Handle terminal window resize event.
 * Updates screen dimensions, enforces minimums, and adjusts cursor/scroll. */
void editor_handle_resize() {
  int new_rows, new_cols;
  if (window_get_size(&new_rows, &new_cols) == -1) return;

  /* Enforce minimum usable dimensions */
  if (new_cols < 10) new_cols = 10;
  if (new_rows < 3) new_rows = 3;

  editor.screen_columns = new_cols;
  /* Reserve rows for UI: status bar + message bar + menu bar (if visible) */
  int reserved = SCREEN_RESERVED_ROWS + (editor.menu_bar_visible ? 1 : 0);
  editor.screen_rows = new_rows - reserved;

  /* Ensure screen_rows is at least 1 */
  if (editor.screen_rows < 1) editor.screen_rows = 1;

  /* Recalculate gutter width */
  editor_update_gutter_width();

  /* Clamp cursor to valid range */
  if (editor.cursor_y >= editor.row_count) {
    editor.cursor_y = editor.row_count > 0 ? editor.row_count - 1 : 0;
  }
  if (editor.cursor_y < editor.row_count && editor.row_count > 0) {
    int rowlen = editor.row[editor.cursor_y].line_size;
    if (editor.cursor_x > rowlen) editor.cursor_x = rowlen;
  }

  /* Reset scroll offsets - editor_scroll() will recalculate */
  editor.row_offset = 0;
  editor.column_offset = 0;

  /* Recalculate wrap breaks if soft wrap is enabled */
  if (editor.soft_wrap) {
    int available_width = editor.screen_columns - editor.gutter_width;
    for (int i = 0; i < editor.row_count; i++) {
      editor_calculate_wrap_breaks(&editor.row[i], available_width);
    }
  }
}

/*** syntax highlighting ***/

/* Check if character is a word separator for syntax highlighting. */
int is_separator(int character) {
  return isspace(character) || character == '\0' || strchr(",.()+-/*=~%<>[];", character) != NULL;
}

/* Update syntax highlighting for a row based on current syntax rules. */
void editor_update_syntax(editor_row *row) {
  row->highlight = realloc(row->highlight, row->render_size);
  memset(row->highlight, HL_NORMAL, row->render_size);

  if (editor.syntax == NULL) return;

  char **keywords = editor.syntax->keywords;

  char *single_comment_start = editor.syntax->singleline_comment_start;
  char *multiline_comment_start = editor.syntax->multiline_comment_start;
  char *multiline_comment_end = editor.syntax->multiline_comment_end;

  int single_comment_start_length = single_comment_start ? strlen(single_comment_start) : 0;
  int multiline_comment_start_length = multiline_comment_start ? strlen(multiline_comment_start) : 0;
  int multiline_comment_end_length = multiline_comment_end ? strlen(multiline_comment_end) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->line_index > 0 && editor.row[row->line_index - 1].open_comment);

#ifndef PCRE2_DISABLED
  /* Check PCRE2 patterns first (for preprocessor directives, etc.)
   * These patterns are checked once at line start before character-by-character processing */
  if (editor.syntax_pattern_count > 0 && !in_comment) {
    for (int p = 0; p < editor.syntax_pattern_count; p++) {
      syntax_pattern *pat = &editor.syntax_patterns[p];
      if (!pat->regex || !pat->match_data) continue;

      int rc = pcre2_match(
          pat->regex,
          (PCRE2_SPTR)row->render,
          row->render_size,
          0,  /* start offset */
          0,  /* options */
          pat->match_data,
          NULL
      );

      if (rc >= 0) {
        /* Pattern matched - get match bounds */
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(pat->match_data);
        PCRE2_SIZE match_start = ovector[0];
        PCRE2_SIZE match_end = ovector[1];

        /* Apply highlighting to matched region */
        if (match_end > match_start && match_end <= (PCRE2_SIZE)row->render_size) {
          memset(&row->highlight[match_start], pat->highlight_type, match_end - match_start);
        }
      }
    }
  }
#endif

  int i = 0;
  while (i < row->render_size) {
    char current_char = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

    if (single_comment_start_length && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], single_comment_start, single_comment_start_length)) {
        memset(&row->highlight[i], HL_COMMENT, row->render_size - i);
        break;
      }
    }

    if (multiline_comment_start_length && multiline_comment_end_length && !in_string) {
      if (in_comment) {
        row->highlight[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], multiline_comment_end, multiline_comment_end_length)) {
          memset(&row->highlight[i], HL_MLCOMMENT, multiline_comment_end_length);
          i += multiline_comment_end_length;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], multiline_comment_start, multiline_comment_start_length)) {
        memset(&row->highlight[i], HL_MLCOMMENT, multiline_comment_start_length);
        i += multiline_comment_start_length;
        in_comment = 1;
        continue;
      }
    }

    if (editor.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->highlight[i] = HL_STRING;
        if (current_char == '\\' && i + 1 < row->render_size) {
          row->highlight[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (current_char == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (current_char == '"' || current_char == '\'') {
          in_string = current_char;
          row->highlight[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (editor.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(current_char) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (current_char == '.' && prev_hl == HL_NUMBER)) {
        row->highlight[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int keyword_index;
      for (keyword_index = 0; keywords[keyword_index]; keyword_index++) {
        int klen = strlen(keywords[keyword_index]);
        int kw2 = keywords[keyword_index][klen - 1] == '|';
        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[keyword_index], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->highlight[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[keyword_index] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(current_char);
    i++;
  }

  int changed = (row->open_comment != in_comment);
  row->open_comment = in_comment;
  if (changed && row->line_index + 1 < editor.row_count)
    editor_update_syntax(&editor.row[row->line_index + 1]);
}

/* Map syntax highlight type to theme color. */
rgb_color editor_syntax_to_color(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return theme_get_color(THEME_SYNTAX_COMMENT);
    case HL_KEYWORD1: return theme_get_color(THEME_SYNTAX_KEYWORD1);
    case HL_KEYWORD2: return theme_get_color(THEME_SYNTAX_KEYWORD2);
    case HL_STRING: return theme_get_color(THEME_SYNTAX_STRING);
    case HL_NUMBER: return theme_get_color(THEME_SYNTAX_NUMBER);
    case HL_MATCH: return theme_get_color(THEME_SYNTAX_MATCH);
    case HL_BRACKET_MATCH: return theme_get_color(THEME_SYNTAX_MATCH);  /* Use same color as search match */
    default: return theme_get_color(THEME_SYNTAX_NORMAL);
  }
}

#ifndef PCRE2_DISABLED
/* Free compiled PCRE2 syntax patterns */
void syntax_free_patterns() {
  if (editor.syntax_patterns) {
    for (int i = 0; i < editor.syntax_pattern_count; i++) {
      if (editor.syntax_patterns[i].match_data) {
        pcre2_match_data_free(editor.syntax_patterns[i].match_data);
      }
      if (editor.syntax_patterns[i].regex) {
        pcre2_code_free(editor.syntax_patterns[i].regex);
      }
    }
    free(editor.syntax_patterns);
    editor.syntax_patterns = NULL;
  }
  editor.syntax_pattern_count = 0;
}
#endif

/* Select syntax highlighting rules based on filename extension.
 * Uses hardcoded HLDB for syntax definitions. */
void editor_select_syntax_highlight() {
  editor.syntax = NULL;
  if (editor.filename == NULL) return;

  char *extension = strrchr(editor.filename, '.');

  /* Match against hardcoded HLDB */
  for (unsigned int entry_index = 0; entry_index < HLDB_ENTRIES; entry_index++) {
    struct editor_syntax *syntax = &HLDB[entry_index];
    unsigned int pattern_index = 0;
    while (syntax->filematch[pattern_index]) {
      int is_extension = (syntax->filematch[pattern_index][0] == '.');
      if ((is_extension && extension && !strcmp(extension, syntax->filematch[pattern_index])) ||
          (!is_extension && strstr(editor.filename, syntax->filematch[pattern_index]))) {
        editor.syntax = syntax;

        int fileditor_row;
        for (fileditor_row = 0; fileditor_row < editor.row_count; fileditor_row++) {
          editor_update_syntax(&editor.row[fileditor_row]);
        }

        return;
      }
      pattern_index++;
    }
  }
}

/*** row operations ***/

/* Convert cursor x position to render x position.
 * Accounts for tab characters which expand to multiple spaces. */
int editor_row_cursor_to_render(editor_row *row, int cx) {
  int rx = 0;
  int char_index;
  for (char_index = 0; char_index < cx; char_index++) {
    if (row->chars[char_index] == '\t')
      rx += (MITER_TAB_STOP - 1) - (rx % MITER_TAB_STOP);
    rx++;
  }
  return rx;
}

/* Convert render x position back to cursor x position.
 * Inverse of editor_row_cursor_to_render for navigating with tabs. */
int editor_row_render_to_cursor(editor_row *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->line_size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (MITER_TAB_STOP - 1) - (cur_rx % MITER_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) return cx;
  }
  return cx;
}

/* Generate the render string from raw chars, expanding tabs to spaces.
 * Also triggers syntax highlighting update for the row. */
void editor_update_row(editor_row *row) {
  int tabs = 0;
  int char_index;
  for (char_index = 0; char_index < row->line_size; char_index++)
    if (row->chars[char_index] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->line_size + tabs*(MITER_TAB_STOP - 1) + 1);

  int render_index = 0;
  for (char_index = 0; char_index < row->line_size; char_index++) {
    if (row->chars[char_index] == '\t') {
      row->render[render_index++] = ' ';
      while (render_index % MITER_TAB_STOP != 0) row->render[render_index++] = ' ';
    } else {
      row->render[render_index++] = row->chars[char_index];
    }
  }
  row->render[render_index] = '\0';
  row->render_size = render_index;

  editor_update_syntax(row);
}

/* Calculate word-boundary wrap break points for soft wrap
 * Returns array of render positions where line should wrap
 * Breaks at word boundaries (spaces, tabs) rather than mid-word */
void editor_calculate_wrap_breaks(editor_row *row, int available_width) {
  /* Free existing breaks */
  free(row->wrap_breaks);
  row->wrap_breaks = NULL;
  row->wrap_break_count = 0;

  /* No wrapping needed */
  if (available_width <= 0 || row->render_size <= available_width) {
    return;
  }

  /* Allocate space for break points (worst case: one break per character) */
  int *breaks = malloc(sizeof(int) * (row->render_size / available_width + 2));
  int break_count = 0;

  /* Start of current line segment */
  int line_start = 0;
  /* Position of last potential break point */
  int last_break_pos = 0;

  for (int i = 0; i < row->render_size; i++) {
    int line_pos = i - line_start;

    /* Check if we're at a potential break point (after space/tab) */
    if (i > 0 && (row->render[i-1] == ' ' || row->render[i-1] == '\t')) {
      last_break_pos = i;
    }

    /* Check if we've exceeded the available width */
    if (line_pos >= available_width) {
      /* Break at last word boundary, or force break if no boundary found */
      int break_pos = last_break_pos > line_start ? last_break_pos : i;

      breaks[break_count++] = break_pos;
      line_start = break_pos;
      last_break_pos = break_pos;
    }
  }

  /* Store the calculated breaks */
  if (break_count > 0) {
    row->wrap_breaks = breaks;
    row->wrap_break_count = break_count;
  } else {
    free(breaks);
  }
}

/* Insert a new row at position 'at' with content 'string' of 'length'. */
void editor_insert_row(int at, char *string, size_t length) {
  if (at < 0 || at > editor.row_count) return;

  editor.row = realloc(editor.row, sizeof(editor_row) * (editor.row_count + 1));
  memmove(&editor.row[at + 1], &editor.row[at], sizeof(editor_row) * (editor.row_count - at));
  for (int row_index = at + 1; row_index <= editor.row_count; row_index++) editor.row[row_index].line_index++;

  editor.row[at].line_index = at;

  editor.row[at].line_size = length;
  editor.row[at].chars = malloc(length + 1);
  memcpy(editor.row[at].chars, string, length);
  editor.row[at].chars[length] = '\0';

  editor.row[at].render_size = 0;
  editor.row[at].render = NULL;
  editor.row[at].highlight = NULL;
  editor.row[at].open_comment = 0;
  /* Mark new row as dirty for SQLite sync */
  editor.row[at].dirty = 1;
  editor.row[at].wrap_breaks = NULL;
  editor.row[at].wrap_break_count = 0;
  editor_update_row(&editor.row[at]);

  editor.row_count++;
  editor.dirty++;
  editor_update_gutter_width();
}

/* Free all memory associated with a row. */
void editor_free_row(editor_row *row) {
  free(row->render);
  free(row->chars);
  free(row->highlight);
  free(row->wrap_breaks);
}

/* Delete the row at index 'at' and shift remaining rows up.
 * Updates line indices and marks buffer as dirty. */
void editor_delete_row(int at) {
  if (at < 0 || at >= editor.row_count) return;
  editor_free_row(&editor.row[at]);
  memmove(&editor.row[at], &editor.row[at + 1], sizeof(editor_row) * (editor.row_count - at - 1));
  for (int row_index = at; row_index < editor.row_count - 1; row_index++) editor.row[row_index].line_index--;
  editor.row_count--;
  editor.dirty++;
  editor_update_gutter_width();
}

/*** selection functions ***/

/* Start a new selection at current cursor position. */
void selection_start() {
  editor.selection.active = 1;
  editor.selection.anchor.row = editor.cursor_y;
  editor.selection.anchor.col = editor.cursor_x;
  editor.selection.cursor = editor.selection.anchor;
  editor.selection.mode = SELECTION_CHAR;
}

/* Extend selection to current cursor position. */
void selection_extend() {
  if (!editor.selection.active) {
    selection_start();
    return;
  }
  editor.selection.cursor.row = editor.cursor_y;
  editor.selection.cursor.col = editor.cursor_x;
}

/* Clear the selection. */
void selection_clear() {
  editor.selection.active = 0;
  /* Note: don't reset click_count here - it's used for multi-click detection
   * which persists across selection clear/start cycles */
}

/* Normalize selection so start <= end.
 * Fills start and end with the ordered positions. */
void selection_normalize(selection_pos *start, selection_pos *end) {
  if (editor.selection.anchor.row < editor.selection.cursor.row ||
      (editor.selection.anchor.row == editor.selection.cursor.row &&
       editor.selection.anchor.col <= editor.selection.cursor.col)) {
    *start = editor.selection.anchor;
    *end = editor.selection.cursor;
  } else {
    *start = editor.selection.cursor;
    *end = editor.selection.anchor;
  }
}

/* Check if a position is within the current selection. */
int selection_contains(int row, int col) {
  if (!editor.selection.active) return 0;

  selection_pos start, end;
  selection_normalize(&start, &end);

  if (row < start.row || row > end.row) return 0;
  if (row == start.row && col < start.col) return 0;
  if (row == end.row && col >= end.col) return 0;

  return 1;
}

/* Select word at position (for double-click). */
void selection_select_word(int row, int col) {
  if (row >= editor.row_count) return;
  editor_row *target_row = &editor.row[row];

  int start = col, end = col;

  /* Expand left to word boundary */
  while (start > 0 && !isspace(target_row->chars[start - 1]) &&
         !ispunct(target_row->chars[start - 1])) {
    start--;
  }

  /* Expand right to word boundary */
  while (end < target_row->line_size && !isspace(target_row->chars[end]) &&
         !ispunct(target_row->chars[end])) {
    end++;
  }

  editor.selection.active = 1;
  editor.selection.anchor.row = row;
  editor.selection.anchor.col = start;
  editor.selection.cursor.row = row;
  editor.selection.cursor.col = end;
  editor.selection.mode = SELECTION_WORD;
}

/* Select entire line (for triple-click). */
void selection_select_line(int row) {
  if (row >= editor.row_count) return;

  editor.selection.active = 1;
  editor.selection.anchor.row = row;
  editor.selection.anchor.col = 0;
  editor.selection.cursor.row = row;
  editor.selection.cursor.col = editor.row[row].line_size;
  editor.selection.mode = SELECTION_LINE;
}

/* Select all text in buffer (Ctrl+A). */
void selection_select_all(void) {
  if (editor.row_count == 0) return;

  /* Set anchor to start of file */
  editor.selection.anchor.row = 0;
  editor.selection.anchor.col = 0;

  /* Set cursor to end of file */
  editor.selection.cursor.row = editor.row_count - 1;
  editor.selection.cursor.col = editor.row[editor.row_count - 1].line_size;

  editor.selection.active = 1;
  editor.selection.mode = SELECTION_CHAR;

  /* Move cursor to end of file */
  editor.cursor_y = editor.row_count - 1;
  editor.cursor_x = editor.row[editor.cursor_y].line_size;
}

/* Extract selected text as a single string.
 * Returns malloc'd string, caller must free. Sets *length to char count. */
char *selection_get_text(int *length) {
  if (!editor.selection.active) {
    *length = 0;
    return NULL;
  }

  selection_pos start, end;
  selection_normalize(&start, &end);

  /* Calculate total size needed */
  int size = 0;
  for (int r = start.row; r <= end.row && r < editor.row_count; r++) {
    int line_start = (r == start.row) ? start.col : 0;
    int line_end = (r == end.row) ? end.col : editor.row[r].line_size;
    size += (line_end - line_start);
    if (r < end.row) size++;  /* newline */
  }

  char *result = malloc(size + 1);
  int pos = 0;

  for (int r = start.row; r <= end.row && r < editor.row_count; r++) {
    int line_start = (r == start.row) ? start.col : 0;
    int line_end = (r == end.row) ? end.col : editor.row[r].line_size;
    int len = line_end - line_start;

    memcpy(result + pos, editor.row[r].chars + line_start, len);
    pos += len;

    if (r < end.row) {
      result[pos++] = '\n';
    }
  }
  result[pos] = '\0';

  *length = size;
  return result;
}

/* Delete currently selected text. */
void selection_delete() {
  if (!editor.selection.active) return;

  selection_pos start, end;
  selection_normalize(&start, &end);

  /* Get selected text for undo log before deleting */
  int sel_length;
  char *selected_text = selection_get_text(&sel_length);

  /* Log the deletion for undo (unless we're in an undo/redo operation) */
  if (selected_text && !editor.undo_logging) {
    undo_log(UNDO_SELECTION_DELETE, start.row, start.col,
             start.row, start.col, NULL,
             end.row, end.col, selected_text);
  }
  free(selected_text);

  /* Position cursor at start of selection */
  editor.cursor_y = start.row;
  editor.cursor_x = start.col;

  if (start.row == end.row) {
    /* Single line deletion */
    editor_row *row = &editor.row[start.row];
    int delete_len = end.col - start.col;
    memmove(&row->chars[start.col], &row->chars[end.col],
            row->line_size - end.col + 1);
    row->line_size -= delete_len;
    editor_update_row(row);
    row->dirty = 1;
  } else {
    /* Multi-line deletion: join first[0:start.col] + last[end.col:] */
    editor_row *first = &editor.row[start.row];
    editor_row *last = &editor.row[end.row];
    int new_size = start.col + (last->line_size - end.col);

    first->chars = realloc(first->chars, new_size + 1);
    memcpy(first->chars + start.col, last->chars + end.col,
           last->line_size - end.col);
    first->line_size = new_size;
    first->chars[new_size] = '\0';
    editor_update_row(first);
    first->dirty = 1;

    /* Delete intermediate and last rows (in reverse order) */
    for (int r = end.row; r > start.row; r--) {
      editor_delete_row(r);
    }
  }

  editor.dirty++;
  selection_clear();
}

/* Delete word backward at all cursor positions using reverse-order pass. */
static void multicursor_delete_word_backward_all() {
  size_t total;
  cursor_position *all = multicursor_collect_all(&total, 1);
  if (!all) return;

  bool *is_primary = multicursor_mark_primary(all, total);
  if (!is_primary) {
    free(all);
    return;
  }

  for (size_t i = 0; i < total; i++) {
    int line = all[i].line;
    int col = all[i].column;

    if (line < 0 || line >= editor.row_count) continue;

    /* If at column 0 and not first line, merge with previous line */
    if (col == 0) {
      if (line == 0) continue;
      int prev_len = editor.row[line - 1].line_size;
      editor_row_append_string(&editor.row[line - 1],
                               editor.row[line].chars,
                               editor.row[line].line_size);
      editor_delete_row(line);
      all[i].line = line - 1;
      all[i].column = prev_len;

      /* Adjust other cursor line numbers below the deleted line */
      for (size_t j = 0; j < total; j++) {
        if (j == i) continue;
        if (all[j].line > line) {
          all[j].line--;
        }
      }
      continue;
    }

    editor_row *row = &editor.row[line];
    int start_x = col;
    int x = col;

    while (x > 0 && !is_word_char(row->chars[x - 1])) {
      x--;
    }
    while (x > 0 && is_word_char(row->chars[x - 1])) {
      x--;
    }

    int delete_len = start_x - x;
    if (delete_len <= 0) continue;

    memmove(&row->chars[x], &row->chars[start_x], row->line_size - start_x + 1);
    row->line_size -= delete_len;
    editor_update_row(row);
    row->dirty = 1;
    editor.dirty++;

    all[i].column = x;
  }

  /* Restore positions */
  size_t sec_idx = 0;
  for (size_t i = 0; i < total; i++) {
    if (is_primary[i]) {
      editor.cursor_y = all[i].line;
      editor.cursor_x = all[i].column;
    } else if (sec_idx < editor.cursor_count) {
      editor.cursors[sec_idx].line = all[i].line;
      editor.cursors[sec_idx].column = all[i].column;
      sec_idx++;
    }
  }

  free(is_primary);
  free(all);
  multicursor_remove_duplicates();

}

/* Delete word forward at all cursor positions using reverse-order pass. */
static void multicursor_delete_word_forward_all() {
  size_t total;
  cursor_position *all = multicursor_collect_all(&total, 1);
  if (!all) return;

  bool *is_primary = multicursor_mark_primary(all, total);
  if (!is_primary) {
    free(all);
    return;
  }

  for (size_t i = 0; i < total; i++) {
    int line = all[i].line;
    int col = all[i].column;
    if (line < 0 || line >= editor.row_count) continue;

    editor_row *row = &editor.row[line];
    if (col >= row->line_size) {
      /* If at end, merge with next line if it exists */
      if (line < editor.row_count - 1) {
        int prev_len = row->line_size;
        editor_row_append_string(row, editor.row[line + 1].chars,
                                 editor.row[line + 1].line_size);
        editor_delete_row(line + 1);
        all[i].column = prev_len;

        for (size_t j = 0; j < total; j++) {
          if (j == i) continue;
          if (all[j].line > line + 1) {
            all[j].line--;
          } else if (all[j].line == line + 1) {
            all[j].line = line;
            all[j].column += prev_len;
          }
        }
      }
      continue;
    }

    int x = col;
    while (x < row->line_size && is_word_char(row->chars[x])) {
      x++;
    }
    while (x < row->line_size && !is_word_char(row->chars[x])) {
      x++;
    }

    int delete_len = x - col;
    if (delete_len <= 0) continue;

    memmove(&row->chars[col], &row->chars[x], row->line_size - x + 1);
    row->line_size -= delete_len;
    editor_update_row(row);
    row->dirty = 1;
    editor.dirty++;
    /* Column stays at original col */
  }

  size_t sec_idx = 0;
  for (size_t i = 0; i < total; i++) {
    if (is_primary[i]) {
      editor.cursor_y = all[i].line;
      editor.cursor_x = all[i].column;
    } else if (sec_idx < editor.cursor_count) {
      editor.cursors[sec_idx].line = all[i].line;
      editor.cursors[sec_idx].column = all[i].column;
      sec_idx++;
    }
  }

  free(is_primary);
  free(all);
  multicursor_remove_duplicates();

}

/* Detect multi-click (double/triple click). */
void selection_detect_multi_click(int row, int col) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* Calculate time difference in milliseconds */
  long ms_diff = (now.tv_sec - editor.selection.last_click_time.tv_sec) * 1000 +
                 (now.tv_nsec - editor.selection.last_click_time.tv_nsec) / 1000000;

  int last_row = editor.selection.last_click_pos.row;
  int last_col = editor.selection.last_click_pos.col;
  int pos_match = (row == last_row && abs(col - last_col) <= 2);

  /* Check if follow-up click in same position within 400ms window */
  if (ms_diff < 400 && pos_match) {
    editor.selection.click_count = (editor.selection.click_count % 3) + 1;
  } else {
    editor.selection.click_count = 1;
  }

  editor.selection.last_click_time = now;
  editor.selection.last_click_pos.row = row;
  editor.selection.last_click_pos.col = col;
}

/*** multi-cursor operations ***/

/* Add a secondary cursor at the specified position.
 * Returns 1 on success, 0 if cursor already exists at position. */
int multicursor_add(int line, int column) {
  /* Check if cursor already exists at this position */
  if (editor.cursor_y == line && editor.cursor_x == column) {
    return 0;  /* Primary cursor is already here */
  }
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == line && editor.cursors[i].column == column) {
      return 0;  /* Secondary cursor already exists here */
    }
  }

  /* Expand array if needed */
  if (editor.cursor_count >= editor.cursor_capacity) {
    size_t new_capacity = editor.cursor_capacity == 0 ? 4 : editor.cursor_capacity * 2;
    cursor_position *new_cursors = realloc(editor.cursors,
                                           new_capacity * sizeof(cursor_position));
    if (new_cursors == NULL) return 0;
    editor.cursors = new_cursors;
    editor.cursor_capacity = new_capacity;
  }

  /* Add new cursor */
  editor.cursors[editor.cursor_count].line = line;
  editor.cursors[editor.cursor_count].column = column;
  editor.cursors[editor.cursor_count].has_selection = 0;
  editor.cursors[editor.cursor_count].anchor_line = 0;
  editor.cursors[editor.cursor_count].anchor_column = 0;
  editor.cursor_count++;

  return 1;
}

/* Add a secondary cursor on the line above the primary cursor.
 * Tries to maintain column alignment. */
void multicursor_add_above() {
  if (editor.cursor_y <= 0) return;

  int target_line = editor.cursor_y - 1;
  int target_col = editor.cursor_x;

  /* Clamp column to line length */
  if (target_line < editor.row_count) {
    if (target_col > editor.row[target_line].line_size) {
      target_col = editor.row[target_line].line_size;
    }
  }

  if (multicursor_add(target_line, target_col)) {
    editor_set_status_message("Added cursor at line %d (total: %zu)",
                              target_line + 1, editor.cursor_count + 1);
  }
  editor.cursors_follow_primary = 1;
  editor.allow_primary_overlap = 0;
}

/* Add a secondary cursor on the line below the primary cursor.
 * Tries to maintain column alignment. */
void multicursor_add_below() {
  if (editor.cursor_y >= editor.row_count - 1) return;

  int target_line = editor.cursor_y + 1;
  int target_col = editor.cursor_x;

  /* Clamp column to line length */
  if (target_line < editor.row_count) {
    if (target_col > editor.row[target_line].line_size) {
      target_col = editor.row[target_line].line_size;
    }
  }

  if (multicursor_add(target_line, target_col)) {
    editor_set_status_message("Added cursor at line %d (total: %zu)",
                              target_line + 1, editor.cursor_count + 1);
  }
  editor.cursors_follow_primary = 1;
  editor.allow_primary_overlap = 0;
}

/* Place a secondary cursor at the primary cursor position (manual drop). */
void multicursor_add_at_primary() {
  /* Avoid duplicate secondary at same location */
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == editor.cursor_y &&
        editor.cursors[i].column == editor.cursor_x) {
      editor_set_status_message("Cursor already placed here");
      return;
    }
  }

  /* Expand array if needed */
  if (editor.cursor_count >= editor.cursor_capacity) {
    size_t new_capacity = editor.cursor_capacity == 0 ? 4 : editor.cursor_capacity * 2;
    cursor_position *new_cursors = realloc(editor.cursors,
                                           new_capacity * sizeof(cursor_position));
    if (new_cursors == NULL) return;
    editor.cursors = new_cursors;
    editor.cursor_capacity = new_capacity;
  }

  editor.cursors[editor.cursor_count].line = editor.cursor_y;
  editor.cursors[editor.cursor_count].column = editor.cursor_x;
  editor.cursors[editor.cursor_count].has_selection = 0;
  editor.cursors[editor.cursor_count].anchor_line = 0;
  editor.cursors[editor.cursor_count].anchor_column = 0;
  editor.cursor_count++;

  editor_set_status_message("Placed cursor at line %d (total: %zu)",
                            editor.cursor_y + 1, editor.cursor_count + 1);
  editor.cursors_follow_primary = 0;     /* Freeze existing cursors in place */
  editor.allow_primary_overlap = 1;      /* Keep overlap until movement relinks */
}

/* Place a cursor at primary position, then advance primary down one line. */
void multicursor_add_at_primary_and_advance() {
  /* Add cursor at current position */
  multicursor_add_at_primary();

  /* Move primary cursor down (if possible) */
  if (editor.cursor_y < editor.row_count - 1) {
    editor.cursor_y++;
    if (editor.cursor_y < editor.row_count) {
      int line_len = editor.row[editor.cursor_y].line_size;
      if (editor.cursor_x > line_len) {
        editor.cursor_x = line_len;
      }
    }
  }

  multicursor_remove_duplicates();
  editor_set_status_message("Placed and moved to line %d (total: %zu)",
                            editor.cursor_y + 1, editor.cursor_count + 1);
  editor.cursors_follow_primary = 1;
  editor.allow_primary_overlap = 0;
}

/* Clear all secondary cursors, keeping only the primary cursor. */
void multicursor_clear() {
  editor.cursor_count = 0;
  /* Keep the allocated memory for reuse */
}

/* Free all multi-cursor memory. Called on editor shutdown. */
void multicursor_free() {
  free(editor.cursors);
  editor.cursors = NULL;
  editor.cursor_count = 0;
  editor.cursor_capacity = 0;
}

/* Comparison function for sorting cursors by position (line, then column).
 * Used for processing cursors in document order. */
static int cursor_cmp_forward(const void *a, const void *b) {
  const cursor_position *ca = (const cursor_position *)a;
  const cursor_position *cb = (const cursor_position *)b;
  if (ca->line != cb->line) return ca->line - cb->line;
  return ca->column - cb->column;
}

/* Comparison function for sorting cursors in reverse order (for deletions).
 * Processing from end to start avoids position adjustment issues. */
static int cursor_cmp_reverse(const void *a, const void *b) {
  return -cursor_cmp_forward(a, b);
}

/* Collect all cursors (primary + secondary) into a single sorted array.
 * Returns malloc'd array, caller must free. Sets *count to array size.
 * If reverse is non-zero, sorts from end to start (for deletions). */
cursor_position *multicursor_collect_all(size_t *count, int reverse) {
  size_t total = 1 + editor.cursor_count;  /* Primary + secondary */
  cursor_position *all = malloc(total * sizeof(cursor_position));
  if (!all) {
    *count = 0;
    return NULL;
  }

  /* Add primary cursor */
  all[0].line = editor.cursor_y;
  all[0].column = editor.cursor_x;
  all[0].has_selection = editor.selection.active;
  all[0].anchor_line = editor.selection.anchor.row;
  all[0].anchor_column = editor.selection.anchor.col;

  /* Add secondary cursors */
  for (size_t i = 0; i < editor.cursor_count; i++) {
    all[i + 1] = editor.cursors[i];
  }

  /* Sort by position */
  qsort(all, total, sizeof(cursor_position),
        reverse ? cursor_cmp_reverse : cursor_cmp_forward);

  *count = total;
  return all;
}

/* Remove duplicate cursors that ended up at the same position.
 * This can happen after edits that merge lines. */
void multicursor_remove_duplicates() {
  if (editor.cursor_count == 0) return;

  /* First pass: drop any secondary cursors that overlap primary */
  size_t write_idx = 0;
  int kept_primary_overlap = 0;
  for (size_t i = 0; i < editor.cursor_count; i++) {
    cursor_position *cur = &editor.cursors[i];
    if (cur->line == editor.cursor_y && cur->column == editor.cursor_x) {
      if (editor.allow_primary_overlap && !kept_primary_overlap) {
        kept_primary_overlap = 1;
      } else {
        continue;
      }
    }
    if (write_idx != i) {
      editor.cursors[write_idx] = *cur;
    }
    write_idx++;
  }
  editor.cursor_count = write_idx;

  if (editor.cursor_count <= 1) return; /* Nothing left to dedup */

  /* Sort secondary cursors by position to make dedup O(n log n) */
  qsort(editor.cursors, editor.cursor_count, sizeof(cursor_position),
        cursor_cmp_forward);

  /* Second pass: collapse adjacent duplicates */
  write_idx = 1;
  for (size_t i = 1; i < editor.cursor_count; i++) {
    cursor_position *prev = &editor.cursors[write_idx - 1];
    cursor_position *cur = &editor.cursors[i];
    if (cur->line == prev->line && cur->column == prev->column) {
      continue;
    }
    if (write_idx != i) {
      editor.cursors[write_idx] = *cur;
    }
    write_idx++;
  }
  editor.cursor_count = write_idx;
}

/* Adjust all cursor positions after a character insert at (line, col).
 * Cursors on the same line at or after col shift right by 1. */
void multicursor_adjust_after_insert(int line, int col) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == line && editor.cursors[i].column >= col) {
      editor.cursors[i].column++;
    }
  }
}

/* Adjust all cursor positions after a character delete at (line, col).
 * Cursors on the same line after col shift left by 1. */
void multicursor_adjust_after_delete(int line, int col) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == line && editor.cursors[i].column > col) {
      editor.cursors[i].column--;
    }
  }
}

/* Adjust all cursor positions after a newline insert at (line, col).
 * Cursors below shift down. Cursors on same line at or after col move to new line. */
void multicursor_adjust_after_newline(int line, int col) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line > line) {
      /* Below the insert: shift down */
      editor.cursors[i].line++;
    } else if (editor.cursors[i].line == line && editor.cursors[i].column >= col) {
      /* On same line at/after insert point: move to new line */
      editor.cursors[i].line++;
      editor.cursors[i].column -= col;
    }
  }
}

/* Adjust all cursor positions after a line merge (backspace at line start).
 * Line `line` was merged into `line-1` at column `merge_col`. */
void multicursor_adjust_after_line_merge(int line, int merge_col) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == line) {
      /* Cursor was on merged line: move to previous line */
      editor.cursors[i].line--;
      editor.cursors[i].column += merge_col;
    } else if (editor.cursors[i].line > line) {
      /* Below the merge: shift up */
      editor.cursors[i].line--;
    }
  }
}

/* Move a single cursor in a given direction, handling line boundaries.
 * Used to apply movement to secondary cursors. */
void multicursor_move_single(cursor_position *cursor, int key) {
  switch (key) {
    case ARROW_LEFT:
      if (cursor->column > 0) {
        cursor->column--;
      } else if (cursor->line > 0) {
        cursor->line--;
        if (cursor->line < editor.row_count) {
          cursor->column = editor.row[cursor->line].line_size;
        }
      }
      break;
    case ARROW_RIGHT:
      if (cursor->line < editor.row_count) {
        if (cursor->column < editor.row[cursor->line].line_size) {
          cursor->column++;
        } else if (cursor->line < editor.row_count - 1) {
          cursor->line++;
          cursor->column = 0;
        }
      }
      break;
    case ARROW_UP:
      if (cursor->line > 0) {
        cursor->line--;
        /* Clamp column to new line's length */
        if (cursor->line < editor.row_count &&
            cursor->column > editor.row[cursor->line].line_size) {
          cursor->column = editor.row[cursor->line].line_size;
        }
      }
      break;
    case ARROW_DOWN:
      if (cursor->line < editor.row_count - 1) {
        cursor->line++;
        /* Clamp column to new line's length */
        if (cursor->line < editor.row_count &&
            cursor->column > editor.row[cursor->line].line_size) {
          cursor->column = editor.row[cursor->line].line_size;
        }
      }
      break;
  }
}

/* Move all secondary cursors in the same direction as the primary cursor.
 * Should be called after moving the primary cursor. */
void multicursor_move_all(int key) {
  if (!editor.cursors_follow_primary) return;
  for (size_t i = 0; i < editor.cursor_count; i++) {
    multicursor_move_single(&editor.cursors[i], key);
  }
  multicursor_remove_duplicates();
}

/* Apply a vertical delta to all secondary cursors (used for Page Up/Down). */
static void multicursor_apply_vertical_delta(int delta_rows) {
  if (!editor.cursors_follow_primary) return;
  if (delta_rows == 0) return;
  for (size_t i = 0; i < editor.cursor_count; i++) {
    int new_line = editor.cursors[i].line + delta_rows;
    if (new_line < 0) new_line = 0;
    if (editor.row_count > 0 && new_line >= editor.row_count) {
      new_line = editor.row_count - 1;
    }
    editor.cursors[i].line = new_line;
    if (new_line < editor.row_count) {
      int line_len = editor.row[new_line].line_size;
      if (editor.cursors[i].column > line_len) {
        editor.cursors[i].column = line_len;
      }
    } else {
      editor.cursors[i].column = 0;
    }
  }
  multicursor_remove_duplicates();
}

/* Move all secondary cursors to either start-of-line or first non-whitespace. */
static void multicursor_apply_home_position(int use_first_nonws) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    int line = editor.cursors[i].line;
    if (line < 0 || line >= editor.row_count) {
      editor.cursors[i].column = 0;
      continue;
    }
    if (use_first_nonws) {
      editor.cursors[i].column = get_first_nonwhitespace_col(&editor.row[line]);
    } else {
      editor.cursors[i].column = 0;
    }
  }
  multicursor_remove_duplicates();
}

/* Move all secondary cursors to end of their lines (END behavior). */
static void multicursor_apply_end_position() {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    int line = editor.cursors[i].line;
    if (line >= 0 && line < editor.row_count) {
      editor.cursors[i].column = editor.row[line].line_size;
    } else {
      editor.cursors[i].column = 0;
    }
  }
  multicursor_remove_duplicates();
}

/* Move a secondary cursor to previous word boundary. */
static void multicursor_move_word_left_single(cursor_position *cur) {
  if (cur->line >= editor.row_count) return;

  if (cur->column == 0) {
    if (cur->line > 0) {
      cur->line--;
      cur->column = editor.row[cur->line].line_size;
    }
    return;
  }

  editor_row *row = &editor.row[cur->line];
  int x = cur->column;
  while (x > 0 && !is_word_char(row->chars[x - 1])) {
    x--;
  }
  while (x > 0 && is_word_char(row->chars[x - 1])) {
    x--;
  }
  cur->column = x;
}

/* Move a secondary cursor to next word boundary. */
static void multicursor_move_word_right_single(cursor_position *cur) {
  if (cur->line >= editor.row_count) return;

  editor_row *row = &editor.row[cur->line];
  if (cur->column >= row->line_size) {
    if (cur->line < editor.row_count - 1) {
      cur->line++;
      cur->column = 0;
    }
    return;
  }

  int x = cur->column;
  while (x < row->line_size && is_word_char(row->chars[x])) {
    x++;
  }
  while (x < row->line_size && !is_word_char(row->chars[x])) {
    x++;
  }
  cur->column = x;
}

static void multicursor_move_word_left_all() {
  if (!editor.cursors_follow_primary) return;
  for (size_t i = 0; i < editor.cursor_count; i++) {
    multicursor_move_word_left_single(&editor.cursors[i]);
  }
  multicursor_remove_duplicates();
}

static void multicursor_move_word_right_all() {
  if (!editor.cursors_follow_primary) return;
  for (size_t i = 0; i < editor.cursor_count; i++) {
    multicursor_move_word_right_single(&editor.cursors[i]);
  }
  multicursor_remove_duplicates();
}

/* Track which collected cursor is primary for restoration. */
static bool *multicursor_mark_primary(cursor_position *all, size_t total) {
  bool *is_primary = malloc(total * sizeof(bool));
  if (!is_primary) return NULL;

  int primary_marked = 0;
  for (size_t i = 0; i < total; i++) {
    if (!primary_marked &&
        all[i].line == editor.cursor_y &&
        all[i].column == editor.cursor_x) {
      is_primary[i] = true;
      primary_marked = 1;
    } else {
      is_primary[i] = false;
    }
  }
  /* Fallback: mark first as primary if none matched (should not happen) */
  if (!primary_marked && total > 0) {
    is_primary[0] = true;
  }
  return is_primary;
}

/* Check if position (line, col) has a secondary cursor.
 * Returns 1 if a secondary cursor exists at that position, 0 otherwise. */
int multicursor_at_position(int line, int col) {
  for (size_t i = 0; i < editor.cursor_count; i++) {
    if (editor.cursors[i].line == line && editor.cursors[i].column == col) {
      return 1;
    }
  }
  return 0;
}

/* Forward declaration for low-level row insert char */
void editor_row_insert_char(editor_row *row, int at, int character);

/* Insert character at a specific position (line, col).
 * Does not move the primary cursor. Used for multi-cursor insert. */
void editor_insert_char_at(int line, int col, int character) {
  if (line < 0 || line > editor.row_count) return;

  if (line == editor.row_count) {
    editor_insert_row(editor.row_count, "", 0);
  }

  editor_row_insert_char(&editor.row[line], col, character);
  editor.dirty++;

  /* Track edit for idle sync */
}

/* Forward declarations for undo system */
void undo_start_new_group(void);
void undo_log(enum undo_op_type type, int cursor_row, int cursor_col,
              int row_idx, int char_pos, const char *char_data,
              int end_row, int end_col, const char *multi_line);

/* Insert character at all cursor positions (primary + secondary).
 * Uses Kilo's pattern: save original positions, operate in reverse order,
 * then restore cursors by matching original positions.
 * Integrates with undo system for atomic undo of all cursor operations. */
void multicursor_insert_char(int character) {
  if (editor.cursor_count == 0) {
    return;  /* No secondary cursors - caller handles normal insert */
  }

  /* Delete selection first if active */
  if (editor.selection.active) {
    selection_delete();
  }

  /* Collect all cursor positions in reverse order (end of file first) */
  size_t total_cursors;
  cursor_position *all_cursors = multicursor_collect_all(&total_cursors, 1);
  if (!all_cursors) return;

  /* Save original positions BEFORE any modifications */
  cursor_position *orig_positions = malloc(total_cursors * sizeof(cursor_position));
  if (!orig_positions) {
    free(all_cursors);
    return;
  }
  memcpy(orig_positions, all_cursors, total_cursors * sizeof(cursor_position));

  /* Force new undo group for atomic multi-cursor operation */
  undo_start_new_group();

  /* Insert at each position (reverse order - end of file first) */
  for (size_t i = 0; i < total_cursors; i++) {
    int line = all_cursors[i].line;
    int col = all_cursors[i].column;

    /* Log undo for each insert */
    char char_str[2] = {(char)character, '\0'};
    undo_log(UNDO_CHAR_INSERT, line, col, line, col, char_str, 0, 0, NULL);

    /* Perform the actual insert */
    editor_insert_char_at(line, col, character);
  }

  /* Auto-unindent closing braces for affected lines */
  if (character == '}') {
    for (size_t i = 0; i < total_cursors; i++) {
      int line = all_cursors[i].line;
      int already = 0;
      for (size_t j = 0; j < i; j++) {
        if (all_cursors[j].line == line) {
          already = 1;
          break;
        }
      }
      if (already) continue;

      int removed = editor_auto_unindent_closing_brace(line);
      if (removed < 0) {
        int rem = -removed;
        for (size_t j = 0; j < total_cursors; j++) {
          if (all_cursors[j].line == line) {
            if (all_cursors[j].column >= rem) {
              all_cursors[j].column -= rem;
            } else {
              all_cursors[j].column = 0;
            }
          }
        }
      }
    }
  }

  /* Calculate new cursor positions using Kilo's algorithm */
  for (size_t i = 0; i < total_cursors; i++) {
    size_t insertions_before = 0;

    /* Count insertions on same line at or before this original column */
    for (size_t j = 0; j < total_cursors; j++) {
      if (orig_positions[j].line == orig_positions[i].line &&
          orig_positions[j].column <= orig_positions[i].column) {
        insertions_before++;
      }
    }

    /* New column = original column + number of insertions before (or at) it */
    all_cursors[i].column = orig_positions[i].column + (int)insertions_before;
  }

  /* Restore primary cursor by matching original position */
  for (size_t i = 0; i < total_cursors; i++) {
    if (orig_positions[i].line == editor.cursor_y &&
        orig_positions[i].column == editor.cursor_x) {
      editor.cursor_x = all_cursors[i].column;
      break;
    }
  }

  /* Restore secondary cursors by matching original positions */
  for (size_t i = 0; i < editor.cursor_count; i++) {
    for (size_t j = 0; j < total_cursors; j++) {
      if (orig_positions[j].line == editor.cursors[i].line &&
          orig_positions[j].column == editor.cursors[i].column) {
        editor.cursors[i].column = all_cursors[j].column;
        break;
      }
    }
  }

  free(orig_positions);
  free(all_cursors);
  multicursor_remove_duplicates();

  /* Track edit for idle sync */
}

/* Forward declaration for low-level row delete char */
void editor_row_delete_char(editor_row *row, int at);
void editor_row_append_string(editor_row *row, char *s, size_t len);
void editor_delete_row(int at);

/* Delete character before a specific position (line, col).
 * Does not move the primary cursor. Used for multi-cursor delete.
 * Returns 1 if a line merge happened, 0 otherwise. */
int editor_delete_char_at(int line, int col) {
  if (line < 0 || line >= editor.row_count) return 0;
  if (col == 0 && line == 0) return 0;

  editor_row *row = &editor.row[line];
  if (col > 0) {
    /* Simple character deletion within a line */
    if (col <= row->line_size) {
      editor_row_delete_char(row, col - 1);
    }
    editor.dirty++;
    return 0;
  } else {
    /* Merge with previous line (backspace at column 0) */
    editor_row_append_string(&editor.row[line - 1], row->chars, row->line_size);
    editor_delete_row(line);
    editor.dirty++;
    return 1;  /* Line merge happened */
  }
}

/* Delete character at all cursor positions (primary + secondary).
 * Uses Kilo's pattern: save original positions, operate in reverse order,
 * then restore cursors by matching original positions.
 * Integrates with undo system for atomic undo of all cursor operations. */
void multicursor_delete_char() {
  if (editor.cursor_count == 0) {
    return;  /* No secondary cursors - caller handles normal delete */
  }

  /* Delete selection first if active */
  if (editor.selection.active) {
    selection_delete();
    return;
  }

  /* Collect all cursor positions in reverse order (end of file first) */
  size_t total_cursors;
  cursor_position *all_cursors = multicursor_collect_all(&total_cursors, 1);
  if (!all_cursors) return;

  /* Save original positions BEFORE any modifications */
  cursor_position *orig_positions = malloc(total_cursors * sizeof(cursor_position));
  if (!orig_positions) {
    free(all_cursors);
    return;
  }
  memcpy(orig_positions, all_cursors, total_cursors * sizeof(cursor_position));

  /* Track which operations merged lines and the prev line length before merge */
  int *line_merged = calloc(total_cursors, sizeof(int));
  int *prev_line_len = calloc(total_cursors, sizeof(int));
  if (!line_merged || !prev_line_len) {
    free(orig_positions);
    free(all_cursors);
    if (line_merged) free(line_merged);
    if (prev_line_len) free(prev_line_len);
    return;
  }

  /* Force new undo group for atomic multi-cursor operation */
  undo_start_new_group();

  /* Delete at each position (reverse order - end of file first) */
  for (size_t i = 0; i < total_cursors; i++) {
    int line = all_cursors[i].line;
    int col = all_cursors[i].column;

    /* Skip if nothing to delete */
    if (line == 0 && col == 0) continue;
    if (line >= editor.row_count) continue;

    if (col > 0) {
      /* Log character delete for undo */
      if (col <= editor.row[line].line_size) {
        char char_str[2] = {editor.row[line].chars[col - 1], '\0'};
        undo_log(UNDO_CHAR_DELETE, line, col, line, col - 1, char_str, 0, 0, NULL);
        editor_row_delete_char(&editor.row[line], col - 1);
        editor.dirty++;
      }
    } else {
      /* Line merge - save previous line length for cursor restore */
      prev_line_len[i] = editor.row[line - 1].line_size;

      /* Log row merge for undo */
      undo_log(UNDO_ROW_DELETE, line, col, line, 0, NULL, 0, 0, NULL);

      /* Perform line merge */
      editor_row_append_string(&editor.row[line - 1],
                               editor.row[line].chars,
                               editor.row[line].line_size);
      editor_delete_row(line);
      editor.dirty++;
      line_merged[i] = 1;

      /* Update cursor to new position (previous line, at merge point) */
      all_cursors[i].line = line - 1;
      all_cursors[i].column = prev_line_len[i];
    }
  }

  /* Calculate new cursor positions using Kilo's algorithm */
  for (size_t i = 0; i < total_cursors; i++) {
    int orig_line = orig_positions[i].line;
    int orig_col = orig_positions[i].column;

    /* Skip if this cursor couldn't delete (was at 0,0) */
    if (orig_line == 0 && orig_col == 0) continue;

    if (line_merged[i]) {
      /* Already updated in the loop above */
      continue;
    }

    /* Count deletions that happened on same line before this cursor */
    int deletions_before = 0;
    int lines_removed_before = 0;

    for (size_t j = 0; j < total_cursors; j++) {
      if (j == i) continue;

      if (line_merged[j]) {
        /* A line merge happened - if it was before our line, adjust line number */
        if (orig_positions[j].line < orig_line ||
            (orig_positions[j].line == orig_line && orig_positions[j].column < orig_col)) {
          lines_removed_before++;
        }
      } else if (orig_positions[j].line == orig_line &&
                 orig_positions[j].column > 0 &&
                 orig_positions[j].column <= orig_col) {
        /* A character deletion on same line before or at our position */
        deletions_before++;
      }
    }

    /* Update position */
    all_cursors[i].line = orig_line - lines_removed_before;
    all_cursors[i].column = orig_col - 1 - deletions_before;  /* -1 for our own deletion */
    if (all_cursors[i].column < 0) all_cursors[i].column = 0;
  }

  /* Restore primary cursor by matching original position */
  for (size_t i = 0; i < total_cursors; i++) {
    if (orig_positions[i].line == editor.cursor_y &&
        orig_positions[i].column == editor.cursor_x) {
      editor.cursor_y = all_cursors[i].line;
      editor.cursor_x = all_cursors[i].column;
      break;
    }
  }

  /* Restore secondary cursors by matching original positions */
  for (size_t i = 0; i < editor.cursor_count; i++) {
    for (size_t j = 0; j < total_cursors; j++) {
      if (orig_positions[j].line == editor.cursors[i].line &&
          orig_positions[j].column == editor.cursors[i].column) {
        editor.cursors[i].line = all_cursors[j].line;
        editor.cursors[i].column = all_cursors[j].column;
        break;
      }
    }
  }

  free(line_merged);
  free(prev_line_len);
  free(orig_positions);
  free(all_cursors);
  multicursor_remove_duplicates();

  /* Track edit for idle sync */
}

/* Forward declarations for row operations */
void editor_insert_row(int at, char *s, size_t len);
void editor_update_row(editor_row *row);

/* Insert newline at a specific position (line, col).
 * Splits the line if col > 0, or inserts empty line above.
 * Returns the number of indent chars added to new line. */
int editor_insert_newline_at(int line, int col) {
  if (line < 0 || line > editor.row_count) return 0;

  /* Add row if at end of file */
  if (line == editor.row_count) {
    editor_insert_row(editor.row_count, "", 0);
    return 0;
  }

  /* Calculate indentation from current line */
  editor_row *current = &editor.row[line];
  int base_indent = editor_line_indentation(current);
  int extra_indent = editor_line_ends_with_opening_brace(current) ? 4 : 0;

  if (col == 0) {
    /* Insert empty row above */
    editor_insert_row(line, "", 0);
  } else {
    /* Split line */
    editor_row *row = &editor.row[line];
    editor_insert_row(line + 1, &row->chars[col], row->line_size - col);
    row = &editor.row[line];
    row->line_size = col;
    row->chars[row->line_size] = '\0';
    editor_update_row(row);
    row->dirty = 1;
  }

  /* Apply indentation to new line */
  int total_indent = base_indent + extra_indent;
  if (total_indent > 0) {
    int new_line = line + 1;
    editor_row *new_row = &editor.row[new_line];
    for (int i = 0; i < total_indent; i++) {
      editor_row_insert_char(new_row, i, ' ');
    }
  }

  editor.dirty++;
  return total_indent;
}

/* Insert newline at all cursor positions (primary + secondary).
 * Uses Kilo's pattern: save original positions, operate in reverse order,
 * then restore cursors by matching original positions.
 * Integrates with undo system for atomic undo of all cursor operations. */
void multicursor_insert_newline() {
  if (editor.cursor_count == 0) {
    return;  /* No secondary cursors - caller handles normal newline */
  }

  /* Delete selection first if active */
  if (editor.selection.active) {
    selection_delete();
  }

  /* Collect all cursor positions in reverse order (end of file first) */
  size_t total_cursors;
  cursor_position *all_cursors = multicursor_collect_all(&total_cursors, 1);
  if (!all_cursors) return;

  /* Save original positions BEFORE any modifications */
  cursor_position *orig_positions = malloc(total_cursors * sizeof(cursor_position));
  if (!orig_positions) {
    free(all_cursors);
    return;
  }
  memcpy(orig_positions, all_cursors, total_cursors * sizeof(cursor_position));

  /* Pre-calculate indentation for each cursor's new line BEFORE any insertions */
  int *new_indents = malloc(total_cursors * sizeof(int));
  if (!new_indents) {
    free(orig_positions);
    free(all_cursors);
    return;
  }

  for (size_t i = 0; i < total_cursors; i++) {
    int line = orig_positions[i].line;

    if (line >= editor.row_count) {
      new_indents[i] = 0;
      continue;
    }

    editor_row *current = &editor.row[line];
    int base_indent = editor_line_indentation(current);
    int extra_indent = editor_line_ends_with_opening_brace(current) ? 4 : 0;
    new_indents[i] = base_indent + extra_indent;
  }

  /* Force new undo group for atomic multi-cursor operation */
  undo_start_new_group();

  /* Insert newlines at all positions in reverse order (end of file first) */
  for (size_t i = 0; i < total_cursors; i++) {
    int line = all_cursors[i].line;
    int col = all_cursors[i].column;

    /* Log undo - use ROW_INSERT for col==0, ROW_SPLIT otherwise */
    if (col == 0) {
      undo_log(UNDO_ROW_INSERT, line, col, line, 0, NULL, 0, 0, NULL);
    } else {
      undo_log(UNDO_ROW_SPLIT, line, col, line, col, NULL, 0, 0, NULL);
    }

    /* Perform the newline insert (this handles indentation internally) */
    editor_insert_newline_at(line, col);
  }

  /* Calculate new cursor positions using Kilo's algorithm */
  for (size_t i = 0; i < total_cursors; i++) {
    int orig_line = orig_positions[i].line;
    int orig_col = orig_positions[i].column;

    /* Count newlines inserted at earlier positions (which shift our line down) */
    int lines_inserted_before = 0;
    for (size_t j = 0; j < total_cursors; j++) {
      int other_line = orig_positions[j].line;
      int other_col = orig_positions[j].column;

      /* A newline before us shifts our line down */
      if (other_line < orig_line ||
          (other_line == orig_line && other_col < orig_col)) {
        lines_inserted_before++;
      }
    }

    /* New position: move to next line (from original), plus offset for earlier inserts */
    all_cursors[i].line = orig_line + 1 + lines_inserted_before;
    all_cursors[i].column = new_indents[i];  /* Position at end of indentation */
  }

  /* Restore primary cursor by matching original position */
  for (size_t i = 0; i < total_cursors; i++) {
    if (orig_positions[i].line == editor.cursor_y &&
        orig_positions[i].column == editor.cursor_x) {
      editor.cursor_y = all_cursors[i].line;
      editor.cursor_x = all_cursors[i].column;
      break;
    }
  }

  /* Restore secondary cursors by matching original positions */
  for (size_t i = 0; i < editor.cursor_count; i++) {
    for (size_t j = 0; j < total_cursors; j++) {
      if (orig_positions[j].line == editor.cursors[i].line &&
          orig_positions[j].column == editor.cursors[i].column) {
        editor.cursors[i].line = all_cursors[j].line;
        editor.cursors[i].column = all_cursors[j].column;
        break;
      }
    }
  }

  free(new_indents);
  free(orig_positions);
  free(all_cursors);
  multicursor_remove_duplicates();

  /* Track edit for idle sync */
}

/*** clipboard operations ***/

/* Copy selection to clipboard (both SQLite and system). */
void editor_copy() {
  if (!editor.selection.active) return;

  int length;
  char *text = selection_get_text(&length);
  if (!text) return;

  clipboard_store(text, editor.selection.mode);
  clipboard_sync_to_system(text);

  editor_set_status_message("Copied %d chars", length);
  free(text);
}

/* Cut selection (copy + delete). */
void editor_cut() {
  if (!editor.selection.active) return;

  editor_copy();
  selection_delete();
  editor_set_status_message("Cut to clipboard");
}

/* Paste from clipboard. Uses smart merge to check system clipboard. */
void editor_paste() {
  /* Smart merge: check system clipboard first */
  clipboard_smart_merge();

  int content_type;
  char *text = clipboard_get_latest(&content_type);
  if (!text) {
    editor_set_status_message("Clipboard empty");
    return;
  }

  /* Delete any existing selection first */
  if (editor.selection.active) {
    selection_delete();
  }

  /* Record start position for undo */
  int start_row = editor.cursor_y;
  int start_col = editor.cursor_x;

  /* Temporarily disable undo logging for individual chars */
  editor.undo_logging = 1;

  /* Insert text character by character */
  for (char *p = text; *p; p++) {
    if (*p == '\n') {
      editor_insert_newline();
    } else {
      editor_insert_char(*p);
    }
  }

  /* Log the entire paste as a single undo operation */
  editor.undo_logging = 0;
  undo_log(UNDO_PASTE, start_row, start_col,
           start_row, start_col, NULL,
           editor.cursor_y, editor.cursor_x, text);

  free(text);
  editor_set_status_message("Pasted");
}

/* Reflow paragraph at cursor position
 * Joins all lines in paragraph and re-wraps at wrap_column
 * Preserves indentation and comment markers
 */
void editor_reflow_paragraph() {
  /* Wrapping disabled */
  if (editor.wrap_column == 0) return;

  paragraph_range para = detect_paragraph(editor.cursor_y);

  /* Detect prefix from first line */
  line_prefix prefix = detect_line_prefix(&editor.row[para.start_line]);

  /* Check if single line needs wrapping */
  if (para.start_line == para.end_line) {
    int available_width = editor.wrap_column - prefix.length;
    if (editor.row[para.start_line].line_size <= available_width) {
      if (prefix.prefix) free(prefix.prefix);
      editor_set_status_message("Line already fits within wrap column %d", editor.wrap_column);
      return;
    }
    /* Single long line - proceed to wrap it */
  }

  /* Calculate total length needed for joined text */
  int total_length = 0;
  for (int i = para.start_line; i <= para.end_line; i++) {
    /* +1 for space */
    total_length += editor.row[i].line_size + 1;
  }

  /* Join all lines into single buffer, removing prefixes */
  char *joined = malloc(total_length + 1);
  int pos = 0;

  for (int i = para.start_line; i <= para.end_line; i++) {
    editor_row *row = &editor.row[i];

    /* Skip prefix on this line */
    int start = 0;
    while (start < row->line_size && (row->chars[start] == ' ' || row->chars[start] == '\t')) {
      start++;
    }
    /* Skip comment markers */
    if (start < row->line_size - 1 && row->chars[start] == '/' && row->chars[start+1] == '/') {
      start += 2;
      if (start < row->line_size && row->chars[start] == ' ') start++;
    } else if (start < row->line_size && row->chars[start] == '*') {
      start++;
      if (start < row->line_size && row->chars[start] == ' ') start++;
    }

    /* Copy content */
    int content_length = row->line_size - start;
    if (content_length > 0) {
      /* Add space between lines */
      if (pos > 0 && !character_is_whitespace(joined[pos-1])) {
        joined[pos++] = ' ';
      }
      memcpy(joined + pos, row->chars + start, content_length);
      pos += content_length;
    }
  }
  joined[pos] = '\0';

  /* Delete original paragraph lines */
  for (int i = para.end_line; i >= para.start_line; i--) {
    editor_delete_row(i);
  }

  /* Re-wrap into lines at wrap_column */
  int current_line = para.start_line;
  int text_pos = 0;
  int wrap_width = editor.wrap_column - prefix.length;

  while (text_pos < pos) {
    /* Skip leading whitespace */
    while (text_pos < pos && character_is_whitespace(joined[text_pos])) {
      text_pos++;
    }

    if (text_pos >= pos) break;

    /* Calculate how much text fits on this line */
    int remaining = pos - text_pos;
    int line_len = remaining < wrap_width ? remaining : wrap_width;

    /* Find word boundary if we're breaking mid-word */
    if (line_len < remaining) {
      int wrap_at = line_len;
      for (int i = line_len; i > 0 && i > line_len - WORD_BREAK_SEARCH_WINDOW; i--) {
        if (character_is_whitespace(joined[text_pos + i])) {
          wrap_at = i;
          break;
        }
      }
      line_len = wrap_at;
    }

    /* Build new line with prefix + text */
    int new_line_size = prefix.length + line_len;
    char *new_line = malloc(new_line_size + 1);

    if (prefix.prefix) {
      memcpy(new_line, prefix.prefix, prefix.length);
    }
    memcpy(new_line + prefix.length, joined + text_pos, line_len);
    new_line[new_line_size] = '\0';

    /* Insert the new line */
    editor_insert_row(current_line, new_line, new_line_size);
    free(new_line);

    text_pos += line_len;
    current_line++;
  }

  free(joined);
  if (prefix.prefix) free(prefix.prefix);

  editor.dirty++;
  editor_set_status_message("Reflowed paragraph at column %d", editor.wrap_column);
}

/* Join (unwrap) paragraph into single line
 * Joins all lines in the paragraph, stripping prefixes
 */
void editor_join_paragraph() {
  paragraph_range para = detect_paragraph(editor.cursor_y);
  line_prefix prefix = detect_line_prefix(&editor.row[para.start_line]);

  /* Check if already single line */
  if (para.start_line == para.end_line) {
    if (prefix.prefix) free(prefix.prefix);
    editor_set_status_message("Already a single line");
    return;
  }

  /* Join all lines, stripping prefixes */
  int total_length = 0;
  for (int i = para.start_line; i <= para.end_line; i++) {
    total_length += editor.row[i].line_size + 1; /* +1 for space */
  }

  char *joined = malloc(total_length + 1);
  int pos = 0;

  for (int i = para.start_line; i <= para.end_line; i++) {
    editor_row *row = &editor.row[i];

    /* Detect and skip prefix on this line */
    line_prefix line_pre = detect_line_prefix(row);
    int start = line_pre.length;
    if (line_pre.prefix) free(line_pre.prefix);

    /* Copy content */
    int content_length = row->line_size - start;
    if (content_length > 0) {
      /* Trim leading whitespace */
      while (start < row->line_size && character_is_whitespace(row->chars[start])) {
        start++;
        content_length--;
      }

      if (content_length > 0) {
        /* Add space between lines */
        if (pos > 0 && !character_is_whitespace(joined[pos-1])) {
          joined[pos++] = ' ';
        }
        memcpy(joined + pos, row->chars + start, content_length);
        pos += content_length;
      }
    }
  }
  joined[pos] = '\0';

  /* Delete original lines */
  for (int i = para.end_line; i >= para.start_line; i--) {
    editor_delete_row(i);
  }

  /* Insert joined line with original prefix */
  int new_line_size = prefix.length + pos;
  char *new_line = malloc(new_line_size + 1);

  if (prefix.prefix) {
    memcpy(new_line, prefix.prefix, prefix.length);
  }
  memcpy(new_line + prefix.length, joined, pos);
  new_line[new_line_size] = '\0';

  editor_insert_row(para.start_line, new_line, new_line_size);

  free(new_line);
  free(joined);
  if (prefix.prefix) free(prefix.prefix);

  editor.dirty++;
  editor_set_status_message("Joined %d lines into 1", para.end_line - para.start_line + 1);
}

/* Insert character at position 'at' within the row.
 * Reallocates row->chars and updates render buffer. */

/* Apply indentation to a specific line. Returns spaces added. */
static int indent_line_apply(int line) {
  if (line < 0 || line >= editor.row_count) return 0;
  editor_row *row = &editor.row[line];
  const int indent_size = 4;

  row->chars = realloc(row->chars, row->line_size + indent_size + 1);
  memmove(&row->chars[indent_size], row->chars, row->line_size + 1);
  for (int i = 0; i < indent_size; i++) {
    row->chars[i] = ' ';
  }
  row->line_size += indent_size;
  editor_update_row(row);
  row->dirty = 1;
  editor.dirty++;
  return indent_size;
}

/* Remove up to 4 leading spaces from a specific line. Returns negative of spaces removed. */
static int unindent_line_apply(int line) {
  if (line < 0 || line >= editor.row_count) return 0;
  editor_row *row = &editor.row[line];
  const int indent_size = 4;
  int spaces_to_remove = 0;

  while (spaces_to_remove < indent_size &&
         spaces_to_remove < row->line_size &&
         row->chars[spaces_to_remove] == ' ') {
    spaces_to_remove++;
  }

  if (spaces_to_remove == 0) return 0;

  memmove(row->chars, row->chars + spaces_to_remove, row->line_size - spaces_to_remove + 1);
  row->line_size -= spaces_to_remove;
  editor_update_row(row);
  row->dirty = 1;
  editor.dirty++;
  return -spaces_to_remove;
}

/* Count leading whitespace of a row. */
int editor_line_indentation(editor_row *row) {
  if (!row) return 0;
  int indent = 0;
  while (indent < row->line_size && (row->chars[indent] == ' ' || row->chars[indent] == '\t')) {
    indent++;
  }
  return indent;
}

/* Check if a row ends with an opening brace after trimming trailing whitespace. */
int editor_line_ends_with_opening_brace(editor_row *row) {
  if (!row || row->line_size == 0) return 0;
  int i = row->line_size;
  while (i > 0 && isspace(row->chars[i - 1])) {
    i--;
  }
  if (i == 0) return 0;
  return row->chars[i - 1] == '{';
}

/* Check if a row starts with a closing brace after leading whitespace. */
static int editor_line_starts_with_closing_brace(editor_row *row) {
  if (!row) return 0;
  int i = 0;
  while (i < row->line_size && isspace(row->chars[i])) {
    i++;
  }
  return (i < row->line_size && row->chars[i] == '}');
}

/* Auto-unindent a line starting with '}' by one indent level. Returns delta (negative if removed). */
int editor_auto_unindent_closing_brace(int line) {
  if (line < 0 || line >= editor.row_count) return 0;
  editor_row *row = &editor.row[line];
  if (!editor_line_starts_with_closing_brace(row)) return 0;
  /* Remove one indent level (4 spaces) if present */
  return unindent_line_apply(line);
}

void editor_row_insert_char(editor_row *row, int at, int character) {
  if (at < 0 || at > row->line_size) at = row->line_size;
  row->chars = realloc(row->chars, row->line_size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->line_size - at + 1);
  row->line_size++;
  row->chars[at] = character;
  editor_update_row(row);
  /* Mark row as dirty for SQLite sync */
  row->dirty = 1;
  editor.dirty++;
}

/* Append 'string' of 'length' to end of the row.
 * Used when joining lines together. */
void editor_row_append_string(editor_row *row, char *string, size_t length) {
  row->chars = realloc(row->chars, row->line_size + length + 1);
  memcpy(&row->chars[row->line_size], string, length);
  row->line_size += length;
  row->chars[row->line_size] = '\0';
  editor_update_row(row);
  /* Mark row as dirty for SQLite sync */
  row->dirty = 1;
  editor.dirty++;
}

/* Delete character at position 'at' within the row.
 * Shifts remaining characters left and updates render buffer. */
void editor_row_delete_char(editor_row *row, int at) {
  if (at < 0 || at >= row->line_size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->line_size - at);
  row->line_size--;
  editor_update_row(row);
  /* Mark row as dirty for SQLite sync */
  row->dirty = 1;
  editor.dirty++;
}

/*** editor operations ***/

  /* Insert character at current cursor position.
   * Creates new row if cursor is past end of file.
   * If selection is active, deletes selected text first. */
void editor_insert_char(int character) {
  /* Handle multi-cursor mode: insert at all cursors */
  if (editor.cursor_count > 0) {
    multicursor_insert_char(character);
    /* Track edit for idle sync */
    return;
  }

  /* Delete selection first if active (typing replaces selection) */
  if (editor.selection.active) {
    selection_delete();
  }

  if (editor.cursor_y == editor.row_count) {
    editor_insert_row(editor.row_count, "", 0);
  }

  /* Log undo before making the change */
  char char_str[2] = {(char)character, '\0'};
  undo_log(UNDO_CHAR_INSERT, editor.cursor_y, editor.cursor_x,
           editor.cursor_y, editor.cursor_x, char_str, 0, 0, NULL);

  editor_row_insert_char(&editor.row[editor.cursor_y], editor.cursor_x, character);
  editor.cursor_x++;

  /* Auto-unindent when typing a closing brace */
  if (character == '}') {
    int removed = editor_auto_unindent_closing_brace(editor.cursor_y);
    if (removed < 0) {
      int rem = -removed;
      if (editor.cursor_x >= rem) {
        editor.cursor_x -= rem;
      } else {
        editor.cursor_x = 0;
      }
    }
  }

  /* Track edit for idle sync */
}

/* Insert newline at cursor, splitting current line if needed.
 * Moves cursor to start of new line.
 * If selection is active, deletes selected text first. */
void editor_insert_newline() {
  /* Handle multi-cursor mode: insert newline at all cursors */
  if (editor.cursor_count > 0) {
    multicursor_insert_newline();
    return;
  }

  /* Delete selection first if active (Enter replaces selection) */
  if (editor.selection.active) {
    selection_delete();
  }

  /* Capture indentation from current line before splitting */
  int indent_len = 0;
  int extra_indent = 0;
  char indent_buf[256] = {0};

  if (editor.cursor_y < editor.row_count) {
    editor_row *current = &editor.row[editor.cursor_y];
    /* Count leading whitespace */
    while (indent_len < current->line_size &&
           indent_len < (int)sizeof(indent_buf) - 5 &&
           (current->chars[indent_len] == ' ' || current->chars[indent_len] == '\t')) {
      indent_buf[indent_len] = current->chars[indent_len];
      indent_len++;
    }

    /* Check if line ends with '{' (before cursor position) for extra indent */
    int check_pos = editor.cursor_x > 0 ? editor.cursor_x - 1 : 0;
    while (check_pos > 0 && isspace(current->chars[check_pos])) {
      check_pos--;
    }
    if (check_pos >= 0 && current->chars[check_pos] == '{') {
      extra_indent = 4;  /* Add 4 spaces for block indent */
    }
  }

  if (editor.cursor_x == 0) {
    /* Log as row insert (empty row at beginning) */
    undo_log(UNDO_ROW_INSERT, editor.cursor_y, editor.cursor_x,
             editor.cursor_y, 0, NULL, 0, 0, NULL);
    editor_insert_row(editor.cursor_y, "", 0);
  } else {
    /* Log as row split (Enter in middle of line) */
    undo_log(UNDO_ROW_SPLIT, editor.cursor_y, editor.cursor_x,
             editor.cursor_y, editor.cursor_x, NULL, 0, 0, NULL);

    editor_row *row = &editor.row[editor.cursor_y];
    editor_insert_row(editor.cursor_y + 1, &row->chars[editor.cursor_x], row->line_size - editor.cursor_x);
    row = &editor.row[editor.cursor_y];
    row->line_size = editor.cursor_x;
    row->chars[row->line_size] = '\0';
    editor_update_row(row);
    /* Mark modified row as dirty */
    row->dirty = 1;
  }
  editor.cursor_y++;
  editor.cursor_x = 0;

  /* Apply auto-indentation to new line */
  if (indent_len > 0 || extra_indent > 0) {
    editor_row *new_row = &editor.row[editor.cursor_y];
    /* Add extra indent spaces */
    for (int i = 0; i < extra_indent; i++) {
      indent_buf[indent_len + i] = ' ';
    }
    int total_indent = indent_len + extra_indent;
    /* Insert indentation at start of new line */
    for (int i = 0; i < total_indent; i++) {
      editor_row_insert_char(new_row, i, indent_buf[i]);
    }
    editor.cursor_x = total_indent;
  }

  /* Track edit for idle sync */
}

/* Indent current line by inserting spaces at the beginning.
 * Uses 4-space indentation. */
void editor_indent_line() {
  if (editor.cursor_y >= editor.row_count) return;

  /* Multi-cursor: indent each unique line once, then restore positions */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 0);
    if (!all) return;

    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = -1;
    int last_delta = 0;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line != last_line) {
        last_delta = indent_line_apply(line);
        last_line = line;
      }
      if (last_delta > 0) {
        all[i].column += last_delta;
      }
      /* Clamp to line length */
      if (line >= 0 && line < editor.row_count) {
        int len = editor.row[line].line_size;
        if (all[i].column > len) all[i].column = len;
      }
    }

    /* Restore positions */
    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    /* Track edit for idle sync */
    return;
  }

  /* Single cursor path */
  int delta = indent_line_apply(editor.cursor_y);
  editor.cursor_x += delta;

}

/* Unindent current line by removing leading spaces.
 * Removes up to 4 spaces from the beginning. */
void editor_unindent_line() {
  if (editor.cursor_y >= editor.row_count) return;

  /* Multi-cursor: unindent each unique line once, then restore positions */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 0);
    if (!all) return;

    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = -1;
    int last_delta = 0;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line != last_line) {
        last_delta = unindent_line_apply(line);
        last_line = line;
      }
      if (last_delta < 0) {
        int removed = -last_delta;
        if (all[i].column >= removed) {
          all[i].column -= removed;
        } else {
          all[i].column = 0;
        }
      }
      if (line >= 0 && line < editor.row_count) {
        int len = editor.row[line].line_size;
        if (all[i].column > len) all[i].column = len;
      }
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  int delta = unindent_line_apply(editor.cursor_y);
  if (delta < 0) {
    int removed = -delta;
    if (editor.cursor_x >= removed) {
      editor.cursor_x -= removed;
    } else {
      editor.cursor_x = 0;
    }
  }

}

/* Delete character before cursor (backspace behavior).
 * Joins lines if cursor is at start of a line.
 * If selection is active, deletes selected text instead. */
void editor_delete_char() {
  /* Handle multi-cursor mode: delete at all cursors */
  if (editor.cursor_count > 0) {
    multicursor_delete_char();
    return;
  }

  /* If selection is active, delete it instead of single char */
  if (editor.selection.active) {
    selection_delete();
    return;
  }

  if (editor.cursor_y == editor.row_count) return;
  if (editor.cursor_x == 0 && editor.cursor_y == 0) return;

  editor_row *row = &editor.row[editor.cursor_y];
  if (editor.cursor_x > 0) {
    /* Log the character being deleted (backspace) */
    char char_str[2] = {row->chars[editor.cursor_x - 1], '\0'};
    undo_log(UNDO_CHAR_DELETE, editor.cursor_y, editor.cursor_x,
             editor.cursor_y, editor.cursor_x - 1, char_str, 0, 0, NULL);

    editor_row_delete_char(row, editor.cursor_x - 1);
    editor.cursor_x--;
  } else {
    /* Log row delete (joining lines via backspace at start) */
    undo_log(UNDO_ROW_DELETE, editor.cursor_y, editor.cursor_x,
             editor.cursor_y, 0, NULL, 0, 0, NULL);

    editor.cursor_x = editor.row[editor.cursor_y - 1].line_size;
    editor_row_append_string(&editor.row[editor.cursor_y - 1], row->chars, row->line_size);
    editor_delete_row(editor.cursor_y);
    editor.cursor_y--;
  }

  /* Track edit for idle sync */
}

/* Duplicate the current line below cursor position */
void editor_duplicate_line() {
  if (editor.cursor_y >= editor.row_count) return;

  /* Multi-cursor: duplicate each unique line once, bottom to top */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 1); /* reverse order */
    if (!all) return;

    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = -1;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line == last_line) continue; /* already duplicated */
      if (line < 0 || line >= editor.row_count) continue;

      editor_row *row = &editor.row[line];
      editor_insert_row(line + 1, row->chars, row->line_size);
      editor.dirty++;

      /* Adjust cursors below insertion point */
      for (size_t j = 0; j < total; j++) {
        if (all[j].line > line) {
          all[j].line++;
        }
      }

      /* Move cursors on this line to duplicated line */
      for (size_t j = 0; j < total; j++) {
        if (all[j].line == line) {
          all[j].line = line + 1;
          if (all[j].column > row->line_size) {
            all[j].column = row->line_size;
          }
        }
      }

      last_line = line;
    }

    /* Restore positions */
    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  editor_row *row = &editor.row[editor.cursor_y];
  editor_insert_row(editor.cursor_y + 1, row->chars, row->line_size);
  editor.cursor_y++;
  editor.dirty++;

  /* Track edit for idle sync */
}

/* Delete the current line */
void editor_delete_line() {
  if (editor.cursor_y >= editor.row_count) return;

  /* Multi-cursor: delete each unique line (bottom to top) */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 1); /* reverse order */
    if (!all) return;

    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = -1;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line == last_line) continue; /* already removed */
      if (line < 0 || line >= editor.row_count) continue;

      editor_delete_row(line);
      editor.dirty++;

      for (size_t j = 0; j < total; j++) {
        if (all[j].line > line) {
          all[j].line--;
        } else if (all[j].line == line) {
          /* Stay at same logical line (now next line), column clamp */
          int target_line = line;
          if (target_line >= editor.row_count) target_line = editor.row_count - 1;
          all[j].line = target_line < 0 ? 0 : target_line;
          if (target_line >= 0 && target_line < editor.row_count) {
            int len = editor.row[target_line].line_size;
            if (all[j].column > len) all[j].column = len;
          } else {
            all[j].column = 0;
          }
        }
      }
      last_line = line;
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  editor_delete_row(editor.cursor_y);

  if (editor.cursor_y >= editor.row_count && editor.row_count > 0) {
    editor.cursor_y = editor.row_count - 1;
  }

  if (editor.cursor_y < editor.row_count) {
    int rowlen = editor.row[editor.cursor_y].line_size;
    if (editor.cursor_x > rowlen) {
      editor.cursor_x = rowlen;
    }
  } else {
    editor.cursor_x = 0;
  }

  editor.dirty++;

}

/* Move current line up by one position */
void editor_move_line_up() {
  if (editor.cursor_y <= 0 || editor.cursor_y >= editor.row_count) return;

  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 0);
    if (!all) return;
    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    /* Move unique lines up, skipping top line */
    int last_line = -2;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line <= 0 || line == last_line) continue;
      if (line >= editor.row_count) continue;

      editor_row temp = editor.row[line];
      editor.row[line] = editor.row[line - 1];
      editor.row[line - 1] = temp;
      editor.row[line].line_index = line;
      editor.row[line - 1].line_index = line - 1;
      editor.dirty++;

      /* Update cursor positions affected by the swap */
      for (size_t j = 0; j < total; j++) {
        if (all[j].line == line) {
          all[j].line = line - 1;
        } else if (all[j].line == line - 1) {
          all[j].line = line;
        }
      }
      last_line = line;
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  editor_row temp = editor.row[editor.cursor_y];
  editor.row[editor.cursor_y] = editor.row[editor.cursor_y - 1];
  editor.row[editor.cursor_y - 1] = temp;

  editor.row[editor.cursor_y].line_index = editor.cursor_y;
  editor.row[editor.cursor_y - 1].line_index = editor.cursor_y - 1;

  editor.cursor_y--;
  editor.dirty++;

}

/* Move current line down by one position */
void editor_move_line_down() {
  if (editor.cursor_y >= editor.row_count - 1) return;

  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 1); /* reverse to move bottom first */
    if (!all) return;
    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = editor.row_count + 1;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line >= editor.row_count - 1 || line == last_line) continue;
      if (line < 0) continue;

      editor_row temp = editor.row[line];
      editor.row[line] = editor.row[line + 1];
      editor.row[line + 1] = temp;
      editor.row[line].line_index = line;
      editor.row[line + 1].line_index = line + 1;
      editor.dirty++;

      for (size_t j = 0; j < total; j++) {
        if (all[j].line == line) {
          all[j].line = line + 1;
        } else if (all[j].line == line + 1) {
          all[j].line = line;
        }
      }
      last_line = line;
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  editor_row temp = editor.row[editor.cursor_y];
  editor.row[editor.cursor_y] = editor.row[editor.cursor_y + 1];
  editor.row[editor.cursor_y + 1] = temp;

  editor.row[editor.cursor_y].line_index = editor.cursor_y;
  editor.row[editor.cursor_y + 1].line_index = editor.cursor_y + 1;

  editor.cursor_y++;
  editor.dirty++;

}

/* Join current line with the next line */
void editor_join_lines() {
  if (editor.cursor_y >= editor.row_count - 1) return;

  /* Multi-cursor: join each unique line with its next line, bottom-up */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 1); /* reverse for stability */
    if (!all) return;
    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int last_line = -1;
    for (size_t i = 0; i < total; i++) {
      int line = all[i].line;
      if (line == last_line) continue;
      if (line < 0 || line >= editor.row_count - 1) continue;

      editor_row *current = &editor.row[line];
      editor_row *next = &editor.row[line + 1];

      int join_pos = current->line_size;
      if (current->line_size > 0 && next->line_size > 0 &&
          current->chars[current->line_size - 1] != ' ' &&
          next->chars[0] != ' ') {
        editor_row_append_string(current, " ", 1);
        join_pos++;
      }

      int next_len = next->line_size;
      editor_row_append_string(current, next->chars, next->line_size);
      editor_delete_row(line + 1);
      editor.dirty++;

      int new_len = current->line_size;

      for (size_t j = 0; j < total; j++) {
        if (all[j].line == line) {
          if (all[j].column > new_len) all[j].column = new_len;
        } else if (all[j].line == line + 1) {
          all[j].line = line;
          int new_col = join_pos + all[j].column;
          if (new_col > new_len) new_col = new_len;
          all[j].column = new_col;
        } else if (all[j].line > line + 1) {
          all[j].line--;
        }
      }

      last_line = line;
      (void)next_len; /* silence unused if compiled differently */
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    return;
  }

  /* Single cursor path */
  editor_row *current = &editor.row[editor.cursor_y];
  editor_row *next = &editor.row[editor.cursor_y + 1];

  int join_pos = current->line_size;

  if (current->line_size > 0 && next->line_size > 0 &&
      current->chars[current->line_size - 1] != ' ' &&
      next->chars[0] != ' ') {
    editor_row_append_string(current, " ", 1);
    join_pos++;
  }

  editor_row_append_string(current, next->chars, next->line_size);
  editor_delete_row(editor.cursor_y + 1);

  editor.cursor_x = join_pos;
  editor.dirty++;

}

/*** file i/o ***/

/* Detect if a line is commented with the single-line marker.
 * Outputs first_nonws position and remove_len (marker + optional space). */
static int line_has_line_comment(editor_row *row, const char *comment, int comment_len,
                                 int *first_nonws, int *remove_len) {
  if (!row || !comment) return 0;
  int fnw = 0;
  while (fnw < row->line_size && isspace(row->chars[fnw])) {
    fnw++;
  }
  if (fnw + comment_len <= row->line_size &&
      strncmp(&row->chars[fnw], comment, comment_len) == 0) {
    int rem = comment_len;
    if (fnw + comment_len < row->line_size &&
        row->chars[fnw + comment_len] == ' ') {
      rem++;
    }
    if (first_nonws) *first_nonws = fnw;
    if (remove_len) *remove_len = rem;
    return 1;
  }
  if (first_nonws) *first_nonws = fnw;
  if (remove_len) *remove_len = 0;
  return 0;
}

/* Detect if a line is wrapped in block comment markers.
 * Returns 1 if both markers present; outputs start position and removal lengths. */
static int line_has_block_comment(editor_row *row, const char *start, int start_len,
                                  const char *end, int end_len,
                                  int *start_pos_out, int *start_remove_len,
                                  int *end_pos_out, int *end_remove_len) {
  if (!row || !start || !end) return 0;

  int fnw = 0;
  while (fnw < row->line_size && isspace(row->chars[fnw])) {
    fnw++;
  }

  if (fnw + start_len > row->line_size) return 0;
  if (strncmp(&row->chars[fnw], start, start_len) != 0) return 0;

  int start_space = (fnw + start_len < row->line_size &&
                     row->chars[fnw + start_len] == ' ') ? 1 : 0;

  /* Trim trailing whitespace before checking end marker */
  int end_pos = row->line_size - end_len;
  while (end_pos > fnw && end_pos > 0 &&
         isspace(row->chars[end_pos - 1])) {
    end_pos--;
  }
  if (end_pos < fnw) return 0;
  if (end_pos + end_len > row->line_size) return 0;
  if (strncmp(&row->chars[end_pos], end, end_len) != 0) return 0;

  int end_space_before = (end_pos > 0 && row->chars[end_pos - 1] == ' ') ? 1 : 0;

  if (start_pos_out) *start_pos_out = fnw;
  if (start_remove_len) *start_remove_len = start_len + start_space;
  if (end_pos_out) *end_pos_out = end_pos - end_space_before;
  if (end_remove_len) *end_remove_len = end_len + end_space_before;
  return 1;
}

/* Toggle line comment on current line.
 * If line starts with comment marker, remove it; otherwise add it. */
void editor_toggle_line_comment() {
  if (editor.cursor_y >= editor.row_count) return;
  if (!editor.syntax || !editor.syntax->singleline_comment_start) return;

  char *comment = editor.syntax->singleline_comment_start;
  int comment_len = strlen(comment);

  /* Multi-cursor: toggle all cursor lines consistently */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 0);
    if (!all) return;

    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int all_commented = 1;
    int *first_nonws = malloc(total * sizeof(int));
    int *remove_len = malloc(total * sizeof(int));
    if (!first_nonws || !remove_len) {
      free(is_primary);
      free(all);
      if (first_nonws) free(first_nonws);
      if (remove_len) free(remove_len);
      return;
    }

    for (size_t i = 0; i < total; i++) {
      first_nonws[i] = 0;
      remove_len[i] = 0;
      if (all[i].line >= editor.row_count) continue;
      editor_row *line = &editor.row[all[i].line];
      if (!line_has_line_comment(line, comment, comment_len, &first_nonws[i], &remove_len[i])) {
        all_commented = 0;
      }
    }

    int last_line = -1;
    int delta = 0;
    for (size_t i = 0; i < total; i++) {
      int line_idx = all[i].line;
      if (line_idx < 0 || line_idx >= editor.row_count) continue;

      if (line_idx != last_line) {
        editor_row *line = &editor.row[line_idx];
        int fnw = first_nonws[i];
        int rem_len = remove_len[i];
        delta = 0;

        if (all_commented && rem_len > 0) {
          for (int k = 0; k < rem_len; k++) {
            editor_row_delete_char(line, fnw);
          }
          delta = -rem_len;
        } else if (!all_commented) {
          for (int k = comment_len - 1; k >= 0; k--) {
            editor_row_insert_char(line, fnw, comment[k]);
          }
          editor_row_insert_char(line, fnw + comment_len, ' ');
          delta = comment_len + 1;
        }
        last_line = line_idx;
      }

      if (delta != 0) {
        int fnw = first_nonws[i];
        if (delta > 0) {
          if (all[i].column >= fnw) {
            all[i].column += delta;
          }
        } else {
          int removed = -delta;
          if (all[i].column > fnw) {
            if (all[i].column >= fnw + removed) {
              all[i].column -= removed;
            } else {
              all[i].column = fnw;
            }
          }
        }
        if (line_idx >= 0 && line_idx < editor.row_count) {
          int len = editor.row[line_idx].line_size;
          if (all[i].column > len) all[i].column = len;
        }
      }
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(first_nonws);
    free(remove_len);
    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    editor.dirty++;
    return;
  }

  editor_row *row = &editor.row[editor.cursor_y];

  /* Find first non-whitespace character */
  int first_nonws = 0;
  while (first_nonws < row->line_size && isspace(row->chars[first_nonws])) {
    first_nonws++;
  }

  /* Check if line starts with comment marker (after whitespace) */
  if (first_nonws + comment_len <= row->line_size &&
      strncmp(&row->chars[first_nonws], comment, comment_len) == 0) {
    /* Remove comment marker */
    /* Also remove trailing space if present */
    int remove_len = comment_len;
    if (first_nonws + comment_len < row->line_size &&
        row->chars[first_nonws + comment_len] == ' ') {
      remove_len++;
    }
    for (int i = 0; i < remove_len; i++) {
      editor_row_delete_char(row, first_nonws);
    }
    /* Adjust cursor position */
    if (editor.cursor_x > first_nonws) {
      if (editor.cursor_x >= first_nonws + remove_len) {
        editor.cursor_x -= remove_len;
      } else {
        editor.cursor_x = first_nonws;
      }
    }
  } else {
    /* Add comment marker at first non-whitespace position */
    /* Insert comment followed by space */
    for (int i = comment_len - 1; i >= 0; i--) {
      editor_row_insert_char(row, first_nonws, comment[i]);
    }
    editor_row_insert_char(row, first_nonws + comment_len, ' ');
    /* Adjust cursor position */
    if (editor.cursor_x >= first_nonws) {
      editor.cursor_x += comment_len + 1;
    }
  }

  editor.dirty++;
}

/* Toggle block comment around selection or current line.
 * If no selection, wraps current line in block comment. */
void editor_toggle_block_comment() {
  if (editor.cursor_y >= editor.row_count) return;
  if (!editor.syntax || !editor.syntax->multiline_comment_start ||
      !editor.syntax->multiline_comment_end) return;

  char *start = editor.syntax->multiline_comment_start;
  char *end = editor.syntax->multiline_comment_end;
  int start_len = strlen(start);
  int end_len = strlen(end);

  /* Multi-cursor: toggle block markers on all cursor lines */
  if (editor.cursor_count > 0) {
    size_t total;
    cursor_position *all = multicursor_collect_all(&total, 0);
    if (!all) return;
    bool *is_primary = multicursor_mark_primary(all, total);
    if (!is_primary) {
      free(all);
      return;
    }

    int all_commented = 1;
    int *start_pos = malloc(total * sizeof(int));
    int *start_rem = malloc(total * sizeof(int));
    int *end_pos = malloc(total * sizeof(int));
    int *end_rem = malloc(total * sizeof(int));
    if (!start_pos || !start_rem || !end_pos || !end_rem) {
      free(is_primary);
      free(all);
      if (start_pos) free(start_pos);
      if (start_rem) free(start_rem);
      if (end_pos) free(end_pos);
      if (end_rem) free(end_rem);
      return;
    }

    for (size_t i = 0; i < total; i++) {
      start_pos[i] = start_rem[i] = end_pos[i] = end_rem[i] = 0;
      if (all[i].line >= editor.row_count) {
        all_commented = 0;
        continue;
      }
      editor_row *line = &editor.row[all[i].line];
      if (!line_has_block_comment(line, start, start_len, end, end_len,
                                  &start_pos[i], &start_rem[i],
                                  &end_pos[i], &end_rem[i])) {
        all_commented = 0;
      }
    }

    int last_line = -1;
    for (size_t i = 0; i < total; i++) {
      int line_idx = all[i].line;
      if (line_idx < 0 || line_idx >= editor.row_count) continue;
      if (line_idx == last_line) continue;
      editor_row *line = &editor.row[line_idx];

      if (all_commented && start_rem[i] > 0 && end_rem[i] > 0) {
        /* Uncomment: remove end marker first, then start */
        int end_remove_pos = end_pos[i];
        int end_remove_len = end_rem[i];
        for (int k = 0; k < end_remove_len; k++) {
          editor_row_delete_char(line, end_remove_pos);
        }
        int start_remove_pos = start_pos[i];
        int start_remove_len = start_rem[i];
        for (int k = 0; k < start_remove_len; k++) {
          editor_row_delete_char(line, start_remove_pos);
        }

        /* Adjust cursors on this line */
        int line_len = line->line_size;
        for (size_t j = 0; j < total; j++) {
          if (all[j].line != line_idx) continue;
          if (all[j].column > end_remove_pos) {
            all[j].column -= end_remove_len;
          }
          if (all[j].column > start_remove_pos) {
            all[j].column -= start_remove_len;
            if (all[j].column < start_remove_pos) all[j].column = start_remove_pos;
          }
          if (all[j].column > line_len) all[j].column = line_len;
        }
      } else {
        /* Comment: insert start marker + space at first non-ws, end marker + space at line end */
        int fnw = 0;
        while (fnw < line->line_size && isspace(line->chars[fnw])) fnw++;

        for (int k = start_len - 1; k >= 0; k--) {
          editor_row_insert_char(line, fnw, start[k]);
        }
        editor_row_insert_char(line, fnw + start_len, ' ');
        int end_insert_pos = line->line_size; /* append at end */
        editor_row_insert_char(line, end_insert_pos, ' ');
        for (int k = 0; k < end_len; k++) {
          editor_row_insert_char(line, end_insert_pos + 1 + k, end[k]);
        }

        int delta_start = start_len + 1;
        int delta_end = end_len + 1;
        int line_len = line->line_size;
        for (size_t j = 0; j < total; j++) {
          if (all[j].line != line_idx) continue;
          if (all[j].column >= fnw) {
            all[j].column += delta_start;
          }
          if (all[j].column >= end_insert_pos) {
            all[j].column += delta_end;
          }
          if (all[j].column > line_len) all[j].column = line_len;
        }
      }

      last_line = line_idx;
    }

    size_t sec_idx = 0;
    for (size_t i = 0; i < total; i++) {
      if (is_primary[i]) {
        editor.cursor_y = all[i].line;
        editor.cursor_x = all[i].column;
      } else if (sec_idx < editor.cursor_count) {
        editor.cursors[sec_idx].line = all[i].line;
        editor.cursors[sec_idx].column = all[i].column;
        sec_idx++;
      }
    }

    free(start_pos);
    free(start_rem);
    free(end_pos);
    free(end_rem);
    free(is_primary);
    free(all);
    multicursor_remove_duplicates();

    editor.dirty++;
    return;
  }

  editor_row *row = &editor.row[editor.cursor_y];

  /* Find first non-whitespace character */
  int first_nonws = 0;
  while (first_nonws < row->line_size && isspace(row->chars[first_nonws])) {
    first_nonws++;
  }

  /* Check if line starts with block comment start marker */
  int has_start = (first_nonws + start_len <= row->line_size &&
                   strncmp(&row->chars[first_nonws], start, start_len) == 0);

  /* Check if line ends with block comment end marker */
  int has_end = 0;
  int end_pos = row->line_size - end_len;
  /* Skip trailing whitespace */
  while (end_pos > 0 && isspace(row->chars[end_pos + end_len - 1])) {
    end_pos--;
  }
  if (end_pos >= first_nonws + start_len &&
      strncmp(&row->chars[end_pos], end, end_len) == 0) {
    has_end = 1;
  }

  if (has_start && has_end) {
    /* Remove block comment markers */
    /* Remove end marker first (to preserve positions) */
    for (int i = 0; i < end_len; i++) {
      editor_row_delete_char(row, end_pos);
    }
    /* Remove space before end marker if present */
    if (end_pos > 0 && row->chars[end_pos - 1] == ' ') {
      editor_row_delete_char(row, end_pos - 1);
      end_pos--;
    }

    /* Remove start marker */
    int remove_start = start_len;
    if (first_nonws + start_len < row->line_size &&
        row->chars[first_nonws + start_len] == ' ') {
      remove_start++;
    }
    for (int i = 0; i < remove_start; i++) {
      editor_row_delete_char(row, first_nonws);
    }
  } else {
    /* Add block comment markers */
    /* Insert start marker with space */
    for (int i = start_len - 1; i >= 0; i--) {
      editor_row_insert_char(row, first_nonws, start[i]);
    }
    editor_row_insert_char(row, first_nonws + start_len, ' ');

    /* Find new end of content (skip trailing whitespace) */
    int content_end = row->line_size;
    while (content_end > first_nonws + start_len + 1 &&
           isspace(row->chars[content_end - 1])) {
      content_end--;
    }

    /* Insert end marker with space before */
    editor_row_insert_char(row, content_end, ' ');
    for (int i = 0; i < end_len; i++) {
      editor_row_insert_char(row, content_end + 1 + i, end[i]);
    }
  }

  editor.dirty++;
}

/*** file i/o ***/

/* Convert all editor rows to a single string with newlines.
 * Sets buffer_length to total byte count. Caller must free result. */
char *editor_rows_to_string(int *buffer_length) {
  int total_length = 0;
  int row_index;
  for (row_index = 0; row_index < editor.row_count; row_index++)
    total_length += editor.row[row_index].line_size + 1;
  *buffer_length = total_length;

  char *buffer = malloc(total_length);
  char *write_ptr = buffer;
  for (row_index = 0; row_index < editor.row_count; row_index++) {
    memcpy(write_ptr, editor.row[row_index].chars, editor.row[row_index].line_size);
    write_ptr += editor.row[row_index].line_size;
    *write_ptr = '\n';
    write_ptr++;
  }

  return buffer;
}

/* Open a file and load its contents into the editor buffer. */
void editor_open(char *filename) {
  free(editor.filename);
  editor.filename = strdup(filename);

  editor_select_syntax_highlight();

  FILE *file_pointer = fopen(filename, "r");
  if (!file_pointer) die("fopen");

  char *line = NULL;
  size_t line_capacity = 0;
  ssize_t line_length;
  while ((line_length = getline(&line, &line_capacity, file_pointer)) != -1) {
    while (line_length > 0 && (line[line_length - 1] == '\n' ||
                           line[line_length - 1] == '\r'))
      line_length--;
    editor_insert_row(editor.row_count, line, line_length);
  }
  free(line);
  fclose(file_pointer);
  editor.dirty = 0;

  /* Sync buffer to FTS5 search index */
  /* removed */
}

/* Save the current buffer to disk. Prompts for filename if needed. */
void editor_save() {
  if (editor.filename == NULL) {
    editor.filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
    if (editor.filename == NULL) {
      editor_set_status_message("Save aborted");
      return;
    }
    editor_select_syntax_highlight();
  }

  int length;
  char *buffer = editor_rows_to_string(&length);

  int file_descriptor = open(editor.filename, O_RDWR | O_CREAT, FILE_PERMISSION_DEFAULT);
  if (file_descriptor != -1) {
    if (ftruncate(file_descriptor, length) != -1) {
      if (write(file_descriptor, buffer, length) == length) {
        close(file_descriptor);
        free(buffer);
        editor.dirty = 0;
        /* removed */
        editor_set_status_message("%d bytes written to disk", length);
        return;
      }
    }
    close(file_descriptor);
  }

  free(buffer);
  editor_set_status_message("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

/* Callback for incremental search. Handles navigation keys and
 * executes FTS5 search when query changes. Highlights current match. */
void editor_find_callback(char *query, int key) {
  static int current_result_index = -1;
  static int direction = 1;
  static char *last_query = NULL;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  /* Ignore timeout - no input available (must be first to preserve highlight) */
  if (key == -1) {
    return;
  }

  /* Restore previous highlight */
  if (saved_hl) {
    memcpy(editor.row[saved_hl_line].highlight, saved_hl, editor.row[saved_hl_line].render_size);
    free(saved_hl);
    saved_hl = NULL;
  }

  /* Handle exit keys */
  if (key == '\r' || key == CHAR_ESCAPE) {
    current_result_index = -1;
    direction = 1;
    if (last_query) {
      free(last_query);
      last_query = NULL;
    }
    return;
  }

  /* Handle navigation keys */
  if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    /* New query - reset index and search */
    current_result_index = -1;
    direction = 1;
  }

  /* Check if query changed */
  if (!last_query || strcmp(last_query, query) != 0) {
    /* New query - execute search */
    if (last_query) free(last_query);
    last_query = strdup(query);
    simple_search(query);
    current_result_index = -1;
  }

  /* No results */
  if (editor.search_result_count == 0) {
    return;
  }

  /* Navigate through results */
  if (current_result_index == -1) {
    /* First search or new query */
    if (direction == 1) {
      current_result_index = 0;
    } else {
      current_result_index = editor.search_result_count - 1;
    }
  } else {
    /* Navigate to next/prev result */
    current_result_index += direction;

    /* Wraparound */
    if (current_result_index < 0) {
      current_result_index = editor.search_result_count - 1;
    } else if (current_result_index >= editor.search_result_count) {
      current_result_index = 0;
    }
  }

  /* Apply result */
  search_result *result = &editor.search_results[current_result_index];
  editor_row *row = &editor.row[result->line_number];

  editor.cursor_y = result->line_number;
  editor.cursor_x = editor_row_render_to_cursor(row, result->match_offset);
  /* Force scroll adjustment on next refresh */
  editor.row_offset = editor.row_count;

  /* Ensure match is visible horizontally */
  editor.render_x = result->match_offset;
  if (editor.render_x < editor.column_offset) {
    editor.column_offset = editor.render_x;
  }
  if (editor.render_x >= editor.column_offset + editor.screen_columns - editor.gutter_width) {
    editor.column_offset = editor.render_x - editor.screen_columns + editor.gutter_width + 1;
  }

  /* Highlight the match */
  saved_hl_line = result->line_number;
  saved_hl = malloc(row->render_size);
  memcpy(saved_hl, row->highlight, row->render_size);
  memset(&row->highlight[result->match_offset], HL_MATCH, result->match_length);
}

/* Start interactive search mode. Prompts user for search term
 * and uses FTS5 for incremental search. ESC restores cursor. */
void editor_find() {
  int saved_cx = editor.cursor_x;
  int saved_cy = editor.cursor_y;
  int saved_coloff = editor.column_offset;
  int saved_rowoff = editor.row_offset;

  char *query = editor_prompt("Search: %s (Use ESC/Arrows/Enter)",
                             editor_find_callback);

  if (query) {
    free(query);
  } else {
    editor.cursor_x = saved_cx;
    editor.cursor_y = saved_cy;
    editor.column_offset = saved_coloff;
    editor.row_offset = saved_rowoff;
  }
}

/*** append buffer ***/

/*
 * Dynamic string buffer for building screen output.
 * Grows automatically as data is appended, then written
 * to terminal in a single write() call to prevent flicker.
 */
struct append_buffer {
  /* Heap-allocated character array */
  char *buffer;
  /* Current length of data in buffer */
  int length;
};

/* Initializer for empty append_buffer */
#define ABUF_INIT {NULL, 0}

void set_foreground_rgb(struct append_buffer *ab, rgb_color color);
void set_background_rgb(struct append_buffer *ab, rgb_color color);
void reset_colors(struct append_buffer *ab);

/* Append 'string' of 'length' to the buffer.
 * Automatically grows the buffer as needed. */
void append_buffer_write(struct append_buffer *ab, const char *string, int length) {
  char *new_buffer = realloc(ab->buffer, ab->length + length);

  if (new_buffer == NULL) return;
  memcpy(&new_buffer[ab->length], string, length);
  ab->buffer = new_buffer;
  ab->length += length;
}

/* Free the buffer's allocated memory. */
void append_buffer_destroy(struct append_buffer *ab) {
  free(ab->buffer);
}

/*** output ***/

/* Adjust viewport offsets to keep cursor visible on screen.
 * Handles both normal scrolling and soft-wrap visual rows.
 * When center_scroll is enabled, implements typewriter scrolling
 * to keep cursor near the vertical center of the screen. */
void editor_scroll() {
  editor.render_x = 0;
  if (editor.cursor_y < editor.row_count) {
    editor.render_x = editor_row_cursor_to_render(&editor.row[editor.cursor_y], editor.cursor_x);
  }

  if (editor.soft_wrap) {
    /* With soft wrap, we need to account for wrapped lines
     * Calculate visual row position of cursor */
    int cursor_visual_row = editor_visual_rows_up_to(editor.cursor_y - 1) + editor_cursor_wrap_row();

    if (editor.center_scroll) {
      /* Typewriter scrolling: keep cursor near screen center */
      int target_rowoff = cursor_visual_row - SCREEN_CENTER;

      /* Clamp to valid range */
      if (target_rowoff < 0) target_rowoff = 0;

      /* Calculate max scroll offset (allow one line past end) */
      int total_visual_rows = editor_visual_rows_up_to(editor.row_count - 1);
      int max_rowoff = total_visual_rows - editor.screen_rows + 1;
      if (max_rowoff < 0) max_rowoff = 0;
      if (target_rowoff > max_rowoff) target_rowoff = max_rowoff;

      editor.row_offset = target_rowoff;
    } else {
      /* Edge-triggered scrolling: only scroll when cursor leaves visible area */
      if (cursor_visual_row < editor.row_offset) {
        editor.row_offset = cursor_visual_row;
      }
      if (cursor_visual_row >= editor.row_offset + editor.screen_rows) {
        editor.row_offset = cursor_visual_row - editor.screen_rows + 1;
      }
    }

    /* No horizontal scrolling with soft wrap */
    editor.column_offset = 0;
  } else {
    /* Normal scrolling without soft wrap */
    if (editor.center_scroll) {
      /* Typewriter scrolling: keep cursor near screen center */
      int target_rowoff = editor.cursor_y - SCREEN_CENTER;

      /* Clamp to valid range */
      if (target_rowoff < 0) target_rowoff = 0;

      /* Calculate max scroll offset (allow one line past end) */
      int max_rowoff = editor.row_count - editor.screen_rows + 1;
      if (max_rowoff < 0) max_rowoff = 0;
      if (target_rowoff > max_rowoff) target_rowoff = max_rowoff;

      editor.row_offset = target_rowoff;
    } else {
      /* Edge-triggered scrolling: only scroll when cursor leaves visible area */
      if (editor.cursor_y < editor.row_offset) {
        editor.row_offset = editor.cursor_y;
      }
      if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
        editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
      }
    }

    /* Horizontal scrolling (same for both modes) */
    if (editor.render_x < editor.column_offset) {
      editor.column_offset = editor.render_x;
    }
    if (editor.render_x >= editor.column_offset + editor.screen_columns) {
      editor.column_offset = editor.render_x - editor.screen_columns + 1;
    }
  }

  /* Ensure scroll offsets are never negative (safety check) */
  if (editor.row_offset < 0) editor.row_offset = 0;
  if (editor.column_offset < 0) editor.column_offset = 0;
}

/* Render all visible rows to the append buffer.
 * Draws line numbers, text content with syntax highlighting, and welcome message. */
void editor_draw_rows(struct append_buffer *ab) {
  int screen_row;
  for (screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
    int fileditor_row, wrap_row;
    int valid = editor_visual_to_logical(screen_row + editor.row_offset, &fileditor_row, &wrap_row);

    /* Draw line number gutter if enabled */
    if (editor.show_line_numbers) {
      set_background_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER_BG));

      if (!valid || fileditor_row >= editor.row_count) {
        /* Empty line - just draw spaces */
        set_foreground_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER));
        for (int i = 0; i < editor.gutter_width; i++) {
          append_buffer_write(ab, " ", 1);
        }
      } else if (editor.soft_wrap && wrap_row > 0) {
        /* Continuation of wrapped line - no line number */
        for (int i = 0; i < editor.gutter_width; i++) {
          append_buffer_write(ab, " ", 1);
        }
      } else {
        /* Draw line number (only on first wrap row) */
        char linenum[16];
        int linenum_len = snprintf(linenum, sizeof(linenum), "%d", fileditor_row + 1);
        /* -1 for trailing space */
        int padding = editor.gutter_width - linenum_len - 1;

        /* Color-code line number based on state */
        if (fileditor_row == editor.cursor_y) {
          /* Current line - highest priority */
          set_foreground_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER_CURRENT));
        } else if (editor.row[fileditor_row].dirty) {
          /* Dirty (unsynced) line - amber/orange */
          set_foreground_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER_DIRTY));
        } else {
          /* Normal synced line */
          set_foreground_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER));
        }

        /* Padding before line number */
        for (int i = 0; i < padding; i++) {
          append_buffer_write(ab, " ", 1);
        }

        append_buffer_write(ab, linenum, linenum_len);
        append_buffer_write(ab, " ", 1);
      }

      /* Reset to editor background - current line highlight set below after gutter */
      set_background_rgb(ab, theme_get_color(THEME_UI_BACKGROUND));
      set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
    }

    /* Determine if this is the current line for background highlighting */
    int is_current_line = (fileditor_row == editor.cursor_y);
    rgb_color line_bg = is_current_line ? theme_get_color(THEME_UI_CURRENT_LINE)
                                        : theme_get_color(THEME_UI_BACKGROUND);

    /* Set background for this line (current line highlight or normal) */
    set_background_rgb(ab, line_bg);

    /* Draw file content */
    if (fileditor_row >= editor.row_count) {
      if (editor.row_count == 0 && screen_row == editor.screen_rows / WELCOME_MESSAGE_ROW_DIVISOR) {
        char welcome[WELCOME_BUFFER_SIZE];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Terra editor -- version %s", MITER_VERSION);
        int available_width = editor.screen_columns - editor.gutter_width;
        if (welcomelen > available_width) welcomelen = available_width;
        int padding = (available_width - welcomelen) / 2;
        if (padding) {
          set_foreground_rgb(ab, theme_get_color(THEME_UI_TILDE));
          append_buffer_write(ab, "~", 1);
          padding--;
        }
        while (padding--) append_buffer_write(ab, " ", 1);
        set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
        append_buffer_write(ab, welcome, welcomelen);
      } else {
        set_foreground_rgb(ab, theme_get_color(THEME_UI_TILDE));
        append_buffer_write(ab, "~", 1);
      }
    } else {
      int available_width = editor.screen_columns - editor.gutter_width;

      /* Calculate which portion of the line to show for this wrap segment */
      int line_offset, line_end;
      if (editor.soft_wrap) {
        editor_row *row = &editor.row[fileditor_row];
        editor_calculate_wrap_breaks(row, available_width);
        line_offset = editor_wrap_segment_start(row, wrap_row);
        line_end = editor_wrap_segment_end(row, wrap_row);
      } else {
        line_offset = editor.column_offset;
        line_end = editor.row[fileditor_row].render_size;
      }

      int line_length = line_end - line_offset;
      if (line_length < 0) line_length = 0;
      if (!editor.soft_wrap && line_length > available_width) line_length = available_width;

      char *chars = &editor.row[fileditor_row].render[line_offset];
      unsigned char *highlight = &editor.row[fileditor_row].highlight[line_offset];
      rgb_color current_color = {0, 0, 0};
      int has_color = 0;
      int in_selection = 0;
      int char_index;
      for (char_index = 0; char_index < line_length; char_index++) {
        /* Check if this character is selected */
        int render_col = line_offset + char_index;
        int cursor_col = editor_row_render_to_cursor(&editor.row[fileditor_row], render_col);
          int is_selected = selection_contains(fileditor_row, cursor_col);

          /* Handle selection state changes */
          if (is_selected && !in_selection) {
            set_background_rgb(ab, theme_get_color(THEME_UI_SELECTION_BG));
          set_foreground_rgb(ab, theme_get_color(THEME_UI_SELECTION_FG));
          in_selection = 1;
          has_color = 0;
        } else if (!is_selected && in_selection) {
          /* Restore normal editor colors (current line highlight or normal bg) */
          set_background_rgb(ab, line_bg);
          set_foreground_rgb(ab, theme_get_color(THEME_SYNTAX_NORMAL));
          in_selection = 0;
          has_color = 0;
        }

        if (iscntrl(chars[char_index])) {
          char sym = (chars[char_index] <= 26) ? '@' + chars[char_index] : '?';
          if (!in_selection) {
            append_buffer_write(ab, ESCAPE_REVERSE_VIDEO, ESCAPE_REVERSE_VIDEO_LEN);
          }
          append_buffer_write(ab, &sym, 1);
          if (!in_selection) {
            append_buffer_write(ab, ESCAPE_NORMAL_VIDEO, ESCAPE_NORMAL_VIDEO_LEN);
            set_background_rgb(ab, line_bg);
            if (has_color) {
              set_foreground_rgb(ab, current_color);
            } else {
              set_foreground_rgb(ab, theme_get_color(THEME_SYNTAX_NORMAL));
            }
            if (has_color) {
              set_foreground_rgb(ab, current_color);
            }
          }
        } else if (in_selection) {
          /* Selected text uses selection colors */
          append_buffer_write(ab, &chars[char_index], 1);
        } else {
          /* Check if this is the matching bracket position */
          int is_bracket_match = (editor.bracket_match_row == fileditor_row &&
                                  editor.bracket_match_col == cursor_col);
          int is_bracket_endpoint = 0;
          if (editor.bracket_open_row != -1 && editor.bracket_close_row != -1) {
            if (fileditor_row == editor.bracket_open_row &&
                cursor_col >= editor.bracket_open_col &&
                cursor_col < editor.bracket_open_col + editor.bracket_open_len) {
              is_bracket_endpoint = 1;
            }
            if (fileditor_row == editor.bracket_close_row &&
                cursor_col >= editor.bracket_close_col &&
                cursor_col < editor.bracket_close_col + editor.bracket_close_len) {
              is_bracket_endpoint = 1;
            }
          }

          /* Secondary cursors now rendered via kitty protocol in editor_refresh_screen() */

          if (is_bracket_match || is_bracket_endpoint) {
            /* Underline bracket/comment delimiters for both partners */
            set_foreground_rgb(ab, theme_get_color(THEME_SYNTAX_MATCH));
            append_buffer_write(ab, ESCAPE_UNDERLINE_START, ESCAPE_UNDERLINE_START_LEN);
            append_buffer_write(ab, &chars[char_index], 1);
            append_buffer_write(ab, ESCAPE_UNDERLINE_END, ESCAPE_UNDERLINE_END_LEN);
            set_background_rgb(ab, line_bg);
            set_foreground_rgb(ab, theme_get_color(THEME_SYNTAX_NORMAL));
            has_color = 0;
          } else if (highlight[char_index] == HL_NORMAL) {
            if (has_color) {
              set_foreground_rgb(ab, theme_get_color(THEME_SYNTAX_NORMAL));
              has_color = 0;
            }
            append_buffer_write(ab, &chars[char_index], 1);
          } else {
            rgb_color color = editor_syntax_to_color(highlight[char_index]);
            if (!has_color || !rgb_equal(color, current_color)) {
              current_color = color;
              has_color = 1;
              set_foreground_rgb(ab, color);
            }
            append_buffer_write(ab, &chars[char_index], 1);
          }
        }
      }
      /* Reset if we were in selection at end of line */
      if (in_selection) {
        set_background_rgb(ab, line_bg);
        set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
      } else {
        set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
      }
    }

    /* Clear to end of line with current background (line_bg already set above) */
    append_buffer_write(ab, ESCAPE_CLEAR_LINE, ESCAPE_CLEAR_LINE_LEN);
    /* Reset to normal background for the next line */
    set_background_rgb(ab, theme_get_color(THEME_UI_BACKGROUND));
    append_buffer_write(ab, CRLF, CRLF_LEN);
  }
}

/* Count dirty rows for sync status */
int editor_count_dirty_lines() {
  int count = 0;
  for (int i = 0; i < editor.row_count; i++) {
    if (editor.row[i].dirty) count++;
  }
  return count;
}

/*** Menu bar drawing ***/

/* Draw the menu bar at the top of the screen */
void editor_draw_menu_bar(struct append_buffer *ab) {
  if (!editor.menu_bar_visible) return;

  append_buffer_write(ab, ESCAPE_CLEAR_LINE, ESCAPE_CLEAR_LINE_LEN);
  set_background_rgb(ab, theme_get_color(THEME_UI_STATUS_BG));
  set_foreground_rgb(ab, theme_get_color(THEME_UI_STATUS_FG));

  int x = 0;
  for (int i = 0; i < MENU_COUNT; i++) {
    menus[i].x_position = x;  /* Store for click detection */

    /* Highlight open menu title */
    if (editor.menu_open == i) {
      set_background_rgb(ab, theme_get_color(THEME_UI_SELECTION_BG));
      set_foreground_rgb(ab, theme_get_color(THEME_UI_SELECTION_FG));
    }

    append_buffer_write(ab, " ", 1);
    append_buffer_write(ab, menus[i].title, strlen(menus[i].title));
    append_buffer_write(ab, " ", 1);
    x += strlen(menus[i].title) + 2;

    if (editor.menu_open == i) {
      set_background_rgb(ab, theme_get_color(THEME_UI_STATUS_BG));
      set_foreground_rgb(ab, theme_get_color(THEME_UI_STATUS_FG));
    }
  }

  /* Pad rest of line */
  while (x < editor.screen_columns) {
    append_buffer_write(ab, " ", 1);
    x++;
  }

  reset_colors(ab);
  append_buffer_write(ab, CRLF, CRLF_LEN);
}

/* Calculate the width needed for a menu dropdown */
static int menu_calculate_width(menu_def *menu) {
  int max_width = 0;
  for (int i = 0; i < menu->item_count; i++) {
    if (menu->items[i].label) {
      int w = strlen(menu->items[i].label);
      if (menu->items[i].shortcut) {
        w += 2 + strlen(menu->items[i].shortcut);  /* spacing + shortcut */
      }
      if (w > max_width) max_width = w;
    }
  }
  return max_width + 4;  /* Padding on left and right */
}

/* Draw the dropdown menu if one is open */
void editor_draw_menu_dropdown(struct append_buffer *ab) {
  if (editor.menu_open < 0 || !editor.menu_bar_visible) return;

  menu_def *menu = &menus[editor.menu_open];
  int menu_x = menu->x_position;
  int menu_width = menu_calculate_width(menu);
  menu->width = menu_width;  /* Store for click detection */

  /* Ensure dropdown doesn't go off screen */
  if (menu_x + menu_width > editor.screen_columns) {
    menu_x = editor.screen_columns - menu_width;
    if (menu_x < 0) menu_x = 0;
  }

  /* Draw each menu item */
  for (int i = 0; i < menu->item_count; i++) {
    /* Position cursor: row 2 (1-indexed) + item index, column menu_x+1 */
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", 2 + i, menu_x + 1);
    append_buffer_write(ab, pos_buf, strlen(pos_buf));

    if (menu->items[i].label == NULL) {
      /* Separator line */
      set_background_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER_BG));
      set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
      for (int j = 0; j < menu_width; j++) {
        append_buffer_write(ab, "\xe2\x94\x80", 3);  /* Unicode horizontal line */
      }
    } else {
      /* Regular menu item */
      if (i == editor.menu_selected_item) {
        set_background_rgb(ab, theme_get_color(THEME_UI_SELECTION_BG));
        set_foreground_rgb(ab, theme_get_color(THEME_UI_SELECTION_FG));
      } else {
        set_background_rgb(ab, theme_get_color(THEME_UI_LINE_NUMBER_BG));
        set_foreground_rgb(ab, theme_get_color(THEME_UI_FOREGROUND));
      }

      /* Left padding + label */
      append_buffer_write(ab, " ", 1);
      int label_len = strlen(menu->items[i].label);
      append_buffer_write(ab, menu->items[i].label, label_len);

      /* Calculate padding between label and shortcut */
      int shortcut_len = menu->items[i].shortcut ? strlen(menu->items[i].shortcut) : 0;
      int pad = menu_width - label_len - shortcut_len - 2;  /* -2 for left/right padding */
      for (int j = 0; j < pad; j++) {
        append_buffer_write(ab, " ", 1);
      }

      /* Shortcut (if any) + right padding */
      if (menu->items[i].shortcut) {
        append_buffer_write(ab, menu->items[i].shortcut, shortcut_len);
      }
      append_buffer_write(ab, " ", 1);
    }
  }

  /* Reset colors */
  reset_colors(ab);
}

/* Draw the status bar showing filename, line count, and cursor position.
 * Uses reverse video for visibility. */
void editor_draw_status_bar(struct append_buffer *ab) {
  append_buffer_write(ab, ESCAPE_CLEAR_LINE, ESCAPE_CLEAR_LINE_LEN);
  set_background_rgb(ab, theme_get_color(THEME_UI_STATUS_BG));
  set_foreground_rgb(ab, theme_get_color(THEME_UI_STATUS_FG));

  char status[STATUS_BAR_BUFFER_SIZE], rstatus[STATUS_BAR_BUFFER_SIZE];
  int status_length = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    editor.filename ? editor.filename : "[No Name]", editor.row_count,
    editor.dirty ? "(modified)" : "");

  /* Check if there are dirty lines for sync status */
  int dirty_count = editor_count_dirty_lines();
  const char *sync_status = (dirty_count > 0) ? ESCAPE_STRIKETHROUGH_START "Synced" ESCAPE_STRIKETHROUGH_END : "Synced";

  /* Calculate ANSI escape code length (invisible characters)
   * \x1b[9m (4) + \x1b[29m (5) = 9 */
  int ansi_escape_length = (dirty_count > 0) ? 9 : 0;

  int right_status_length = snprintf(rstatus, sizeof(rstatus), "%s | %s | %s | %d/%d",
    editor.syntax ? editor.syntax->filetype : "no ft", theme_get_name(), sync_status, editor.cursor_y + 1, editor.row_count);

  /* Adjust right_status_length to account for ANSI escape codes */
  int right_status_visible_length = right_status_length - ansi_escape_length;

  if (status_length > editor.screen_columns) status_length = editor.screen_columns;
  append_buffer_write(ab, status, status_length);
  while (status_length < editor.screen_columns) {
    if (editor.screen_columns - status_length == right_status_visible_length) {
      append_buffer_write(ab, rstatus, right_status_length);
      break;
    } else {
      append_buffer_write(ab, " ", 1);
      status_length++;
    }
  }

  reset_colors(ab);
  append_buffer_write(ab, CRLF, CRLF_LEN);
}

/* Draw the message bar showing status messages with timeout.
 * Messages disappear after STATUS_MESSAGE_TIMEOUT_SECONDS. */
void editor_draw_message_bar(struct append_buffer *ab) {
  append_buffer_write(ab, ESCAPE_CLEAR_LINE, ESCAPE_CLEAR_LINE_LEN);
  set_background_rgb(ab, theme_get_color(THEME_UI_MESSAGE_BG));
  set_foreground_rgb(ab, theme_get_color(THEME_UI_MESSAGE_FG));

  int message_length = strlen(editor.status_message);
  if (message_length > editor.screen_columns) message_length = editor.screen_columns;

  int current_column = 0;
  if (message_length && time(NULL) - editor.status_message_time < STATUS_MESSAGE_TIMEOUT_SECONDS) {
    append_buffer_write(ab, editor.status_message, message_length);
    current_column = message_length;
  }

  /* Fill rest of line with spaces to ensure background spans full width */
  while (current_column < editor.screen_columns) {
    append_buffer_write(ab, " ", 1);
    current_column++;
  }

  reset_colors(ab);
}

/* Redraw the entire screen. Builds output in append buffer
 * then writes to terminal in one call to prevent flicker. */
void editor_refresh_screen() {
  editor_scroll();

  /* Update bracket matching state */
  editor_find_matching_bracket();

  struct append_buffer ab = ABUF_INIT;

  /* Set editor background and foreground colors */
  set_background_rgb(&ab, theme_get_color(THEME_UI_BACKGROUND));
  set_foreground_rgb(&ab, theme_get_color(THEME_UI_FOREGROUND));

  append_buffer_write(&ab, ESCAPE_HIDE_CURSOR, ESCAPE_HIDE_CURSOR_LEN);
  append_buffer_write(&ab, ESCAPE_CURSOR_HOME, ESCAPE_CURSOR_HOME_LEN);

  editor_draw_menu_bar(&ab);
  editor_draw_rows(&ab);
  editor_draw_status_bar(&ab);
  editor_draw_message_bar(&ab);
  editor_draw_menu_dropdown(&ab);  /* Draw dropdown overlay last */

  /* Position cursor accounting for menu bar */
  int cursor_row = (editor.cursor_y - editor.row_offset) + 1;
  if (editor.menu_bar_visible) cursor_row++;  /* Offset for menu bar */
  char cursor_buffer[CURSOR_POSITION_BUFFER_SIZE];
  snprintf(cursor_buffer, sizeof(cursor_buffer), ESCAPE_CURSOR_POSITION_FORMAT, cursor_row,
                                            (editor.render_x - editor.column_offset) + editor.gutter_width + 1);
  append_buffer_write(&ab, cursor_buffer, strlen(cursor_buffer));

  /* Render secondary cursors via kitty protocol */
  append_buffer_write(&ab, ESCAPE_KITTY_CURSOR_CLEAR, ESCAPE_KITTY_CURSOR_CLEAR_LEN);
  for (size_t i = 0; i < editor.cursor_count; i++) {
    int file_row = editor.cursors[i].line;
    int file_col = editor.cursors[i].column;

    /* Convert to screen coordinates */
    int screen_row = file_row - editor.row_offset + 1;
    if (editor.menu_bar_visible) screen_row++;

    /* Calculate render_x for this cursor (handle tabs) */
    int render_col = 0;
    if (file_row >= 0 && file_row < editor.row_count) {
      render_col = editor_row_cursor_to_render(&editor.row[file_row], file_col);
    }
    int screen_col = render_col - editor.column_offset + editor.gutter_width + 1;

    /* Skip if off-screen */
    if (screen_row < 1 || screen_row > editor.screen_rows) continue;
    if (screen_col < 1 || screen_col > editor.screen_columns) continue;

    char kitty_buf[32];
    int len = snprintf(kitty_buf, sizeof(kitty_buf), ESCAPE_KITTY_CURSOR_FORMAT, screen_row, screen_col);
    append_buffer_write(&ab, kitty_buf, len);
  }

  append_buffer_write(&ab, ESCAPE_SHOW_CURSOR, ESCAPE_SHOW_CURSOR_LEN);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  append_buffer_destroy(&ab);
}

/* Set a message to display in the message bar.
 * Supports printf-style formatting. Auto-clears after timeout. */
void editor_set_status_message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(editor.status_message, sizeof(editor.status_message), fmt, ap);
  va_end(ap);
  editor.status_message_time = time(NULL);
}

/*** input ***/

/* Display a prompt and read user input.
 * Calls callback on each keystroke for incremental features like search.
 * Returns user input or NULL if cancelled with ESC. */
char *editor_prompt(char *prompt, void (*callback)(char *, int)) {
  size_t buffer_size = PROMPT_INITIAL_BUFFER_SIZE;
  char *buffer = malloc(buffer_size);

  size_t buffer_length = 0;
  buffer[0] = '\0';

  while (1) {
    editor_set_status_message(prompt, buffer);
    editor_refresh_screen();

    int key = editor_read_key();
    if (key == DEL_KEY || key == CTRL_KEY('h') || key == BACKSPACE) {
      if (buffer_length != 0) buffer[--buffer_length] = '\0';
    } else if (key == CHAR_ESCAPE) {
      editor_set_status_message("");
      if (callback) callback(buffer, key);
      free(buffer);
      return NULL;
    } else if (key == '\r') {
      if (buffer_length != 0) {
        editor_set_status_message("");
        if (callback) callback(buffer, key);
        return buffer;
      }
    } else if (key >= 0 && !iscntrl(key) && key < ASCII_MAX) {
      if (buffer_length == buffer_size - 1) {
        buffer_size *= 2;
        buffer = realloc(buffer, buffer_size);
      }
      buffer[buffer_length++] = key;
      buffer[buffer_length] = '\0';
    }

    if (callback) callback(buffer, key);
  }
}

/* Jump to a specific line number (Ctrl+G). */
void editor_jump_to_line(void) {
  char *line_str = editor_prompt("Jump to line: %s (ESC to cancel)", NULL);
  if (line_str == NULL) {
    editor_set_status_message("Jump cancelled");
    return;
  }

  int line = atoi(line_str);
  free(line_str);

  /* Validate (1-indexed for user, 0-indexed internal) */
  if (line < 1 || line > editor.row_count) {
    editor_set_status_message("Invalid line number: %d (valid: 1-%d)", line, editor.row_count);
    return;
  }

  /* Clear selection */
  selection_clear();

  /* Jump to line (convert to 0-indexed) */
  editor.cursor_y = line - 1;
  editor.cursor_x = 0;

  /* Center view on target line */
  int target_rowoff = editor.cursor_y - (editor.screen_rows / 2);
  if (target_rowoff < 0) target_rowoff = 0;
  int max_rowoff = editor.row_count - editor.screen_rows;
  if (max_rowoff < 0) max_rowoff = 0;
  if (target_rowoff > max_rowoff) target_rowoff = max_rowoff;
  editor.row_offset = target_rowoff;

  editor_set_status_message("Jumped to line %d", line);
}

/* Skip past next closing bracket/brace/paren (Alt+]).
 * Searches forward from cursor for unmatched closing pair. */
void editor_skip_closing_pair(void) {
  if (editor.cursor_y >= editor.row_count) return;

  int nesting = 0;

  /* Search forward from cursor for closing bracket at nesting level 0 */
  for (int y = editor.cursor_y; y < editor.row_count; y++) {
    editor_row *row = &editor.row[y];
    int start_col = (y == editor.cursor_y) ? editor.cursor_x : 0;

    for (int x = start_col; x < row->line_size; x++) {
      char c = row->chars[x];

      /* Track nesting */
      if (c == '(' || c == '[' || c == '{') {
        nesting++;
      } else if (c == ')' || c == ']' || c == '}') {
        if (nesting == 0) {
          /* Found matching closer - move cursor past it */
          editor.cursor_y = y;
          editor.cursor_x = x + 1;
          return;
        }
        nesting--;
      }
    }
  }
  /* Not found - cursor stays in place */
}

/* Jump back to position after nearest opening bracket/brace/paren (Alt+[).
 * Searches backward from cursor for unmatched opening pair. */
void editor_skip_opening_pair(void) {
  if (editor.row_count == 0) return;

  int nesting = 0;

  /* Search backward from cursor for opening bracket at nesting level 0 */
  for (int y = editor.cursor_y; y >= 0; y--) {
    editor_row *row = &editor.row[y];
    int start_col = (y == editor.cursor_y) ? editor.cursor_x - 1 : row->line_size - 1;

    for (int x = start_col; x >= 0; x--) {
      char c = row->chars[x];

      /* Track nesting (reversed logic since we're going backward) */
      if (c == ')' || c == ']' || c == '}') {
        nesting++;
      } else if (c == '(' || c == '[' || c == '{') {
        if (nesting == 0) {
          /* Found matching opener - move cursor right after it */
          editor.cursor_y = y;
          editor.cursor_x = x + 1;
          return;
        }
        nesting--;
      }
    }
  }
  /* Not found - cursor stays in place */
}

/*** file browser ***/

/* Structure for file list items in the browser */
typedef struct {
  char *name;           /* Display name (with / for directories) */
  char *actual_name;    /* Actual filename without decorations */
  int is_directory;
} file_list_item;

/* Compare function for sorting file list (directories first, then alphabetical) */
int file_list_compare(const void *a, const void *b) {
  const file_list_item *item_a = (const file_list_item *)a;
  const file_list_item *item_b = (const file_list_item *)b;

  /* Directories sort before files */
  if (item_a->is_directory && !item_b->is_directory) return -1;
  if (!item_a->is_directory && item_b->is_directory) return 1;

  /* Within same type, sort alphabetically (case-insensitive) */
  return strcasecmp(item_a->name, item_b->name);
}

/* Free file list items */
void file_list_free(file_list_item *items, int count) {
  for (int i = 0; i < count; i++) {
    free(items[i].name);
    free(items[i].actual_name);
  }
  free(items);
}

/* Get directory listing */
file_list_item *file_list_get(const char *path, int *count) {
  DIR *dp = opendir(path);
  if (!dp) return NULL;

  int capacity = 64;
  file_list_item *items = malloc(capacity * sizeof(file_list_item));
  *count = 0;

  /* Add parent directory entry if not at root */
  if (strcmp(path, "/") != 0) {
    items[(*count)].name = strdup("../");
    items[(*count)].actual_name = strdup("..");
    items[(*count)].is_directory = 1;
    (*count)++;
  }

  struct dirent *entry;
  while ((entry = readdir(dp))) {
    /* Skip . and .. */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    /* Skip hidden files */
    if (entry->d_name[0] == '.')
      continue;

    /* Resize if needed */
    if (*count >= capacity) {
      capacity *= 2;
      items = realloc(items, capacity * sizeof(file_list_item));
    }

    /* Check if directory */
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
    struct stat st;
    int is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));

    items[*count].actual_name = strdup(entry->d_name);
    if (is_dir) {
      char display[PATH_MAX];
      snprintf(display, sizeof(display) - 1, "%s/", entry->d_name);
      items[*count].name = strdup(display);
    } else {
      items[*count].name = strdup(entry->d_name);
    }
    items[*count].is_directory = is_dir;
    (*count)++;
  }
  closedir(dp);

  /* Sort: directories first (except ../), then alphabetical */
  if (*count > 1) {
    /* Sort everything except the first entry if it's ../ */
    int start = (items[0].actual_name && strcmp(items[0].actual_name, "..") == 0) ? 1 : 0;
    if (*count - start > 1) {
      qsort(&items[start], *count - start, sizeof(file_list_item), file_list_compare);
    }
  }

  return items;
}

/* Draw file browser as a centered half-screen panel */
void file_browser_draw(file_list_item *items, int count, int selected, const char *path, int scroll_offset) {
  struct append_buffer ab = ABUF_INIT;

  /* Calculate panel dimensions - half screen height, 70% width */
  int panel_height = editor.screen_rows / 2;
  if (panel_height < 10) panel_height = 10;
  if (panel_height > editor.screen_rows - 2) panel_height = editor.screen_rows - 2;

  int panel_width = (editor.screen_columns * 70) / 100;
  if (panel_width < 40) panel_width = 40;
  if (panel_width > editor.screen_columns - 4) panel_width = editor.screen_columns - 4;

  /* Center the panel */
  int panel_top = (editor.screen_rows - panel_height) / 2;
  int panel_left = (editor.screen_columns - panel_width) / 2;

  append_buffer_write(&ab, ESCAPE_HIDE_CURSOR, ESCAPE_HIDE_CURSOR_LEN);

  /* Draw each row of the panel */
  for (int row = 0; row < panel_height; row++) {
    /* Position cursor at start of this panel row */
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", panel_top + row + 1, panel_left + 1);
    append_buffer_write(&ab, pos_buf, strlen(pos_buf));

    if (row == 0) {
      /* Header row */
      set_background_rgb(&ab, theme_get_color(THEME_UI_STATUS_BG));
      set_foreground_rgb(&ab, theme_get_color(THEME_UI_STATUS_FG));

      char header[256];
      int header_len = snprintf(header, sizeof(header), " Open: %s", path);
      if (header_len > panel_width) header_len = panel_width;
      append_buffer_write(&ab, header, header_len);

      /* Pad rest of header */
      for (int p = header_len; p < panel_width; p++) {
        append_buffer_write(&ab, " ", 1);
      }
    } else if (row == panel_height - 1) {
      /* Help bar at bottom */
      set_background_rgb(&ab, theme_get_color(THEME_UI_MESSAGE_BG));
      set_foreground_rgb(&ab, theme_get_color(THEME_UI_MESSAGE_FG));

      char help[] = " \xe2\x86\x91\xe2\x86\x93/Scroll:Nav Enter/DblClick:Open ESC:Cancel";
      int help_len = strlen(help);
      if (help_len > panel_width) help_len = panel_width;
      append_buffer_write(&ab, help, help_len);

      /* Pad rest of help bar */
      for (int p = help_len; p < panel_width; p++) {
        append_buffer_write(&ab, " ", 1);
      }
    } else {
      /* File list row */
      int item_idx = scroll_offset + (row - 1);

      if (item_idx < count) {
        file_list_item *item = &items[item_idx];

        /* Highlight selected item */
        if (item_idx == selected) {
          set_background_rgb(&ab, theme_get_color(THEME_UI_SELECTION_BG));
          set_foreground_rgb(&ab, theme_get_color(THEME_UI_SELECTION_FG));
        } else {
          set_background_rgb(&ab, theme_get_color(THEME_UI_LINE_NUMBER_BG));
          /* Directories get different color */
          if (item->is_directory) {
            set_foreground_rgb(&ab, theme_get_color(THEME_SYNTAX_KEYWORD2));
          } else {
            set_foreground_rgb(&ab, theme_get_color(THEME_UI_FOREGROUND));
          }
        }

        /* Print item name with padding */
        append_buffer_write(&ab, " ", 1);
        int name_len = strlen(item->name);
        int max_name = panel_width - 2;
        if (name_len > max_name) name_len = max_name;
        append_buffer_write(&ab, item->name, name_len);

        /* Pad rest of line */
        for (int p = name_len + 1; p < panel_width; p++) {
          append_buffer_write(&ab, " ", 1);
        }
      } else {
        /* Empty row in panel */
        set_background_rgb(&ab, theme_get_color(THEME_UI_LINE_NUMBER_BG));
        set_foreground_rgb(&ab, theme_get_color(THEME_UI_FOREGROUND));
        for (int p = 0; p < panel_width; p++) {
          append_buffer_write(&ab, " ", 1);
        }
      }
    }
  }

  /* Reset colors and show cursor */
  set_background_rgb(&ab, theme_get_color(THEME_UI_BACKGROUND));
  set_foreground_rgb(&ab, theme_get_color(THEME_UI_FOREGROUND));
  append_buffer_write(&ab, ESCAPE_SHOW_CURSOR, ESCAPE_SHOW_CURSOR_LEN);

  write(STDOUT_FILENO, ab.buffer, ab.length);
  append_buffer_destroy(&ab);
}

/* Interactive file browser - returns selected filepath or NULL */
char *editor_file_browser(void) {
  char current_path[PATH_MAX];
  if (getcwd(current_path, sizeof(current_path)) == NULL) {
    strcpy(current_path, "/");
  }

  int selected = 0;
  int scroll_offset = 0;

  /* Mouse double-click tracking */
  struct timespec fb_last_click_time = {0, 0};
  int fb_last_click_item = -1;

  while (1) {
    int count;
    file_list_item *items = file_list_get(current_path, &count);
    if (!items || count == 0) {
      editor_set_status_message("Cannot read directory: %s", current_path);
      if (items) file_list_free(items, count);
      return NULL;
    }

    /* Clamp selection */
    if (selected >= count) selected = count - 1;
    if (selected < 0) selected = 0;

    /* Calculate visible rows (must match file_browser_draw panel calculation) */
    int panel_height = editor.screen_rows / 2;
    if (panel_height < 10) panel_height = 10;
    if (panel_height > editor.screen_rows - 2) panel_height = editor.screen_rows - 2;
    int visible_rows = panel_height - 2;  /* minus header and help bar */

    /* Adjust scroll to keep selection visible */
    if (selected < scroll_offset) {
      scroll_offset = selected;
    } else if (selected >= scroll_offset + visible_rows) {
      scroll_offset = selected - visible_rows + 1;
    }

    /* Draw file browser */
    file_browser_draw(items, count, selected, current_path, scroll_offset);

    int key = editor_read_key();
    int do_open = 0;  /* Flag to trigger open action (Enter or double-click) */

    if (key == CHAR_ESCAPE) {
      file_list_free(items, count);
      return NULL;
    } else if (key == ARROW_UP && selected > 0) {
      selected--;
    } else if (key == ARROW_DOWN && selected < count - 1) {
      selected++;
    } else if (key == PAGE_UP) {
      selected -= visible_rows;
      if (selected < 0) selected = 0;
    } else if (key == PAGE_DOWN) {
      selected += visible_rows;
      if (selected >= count) selected = count - 1;
    } else if (key == HOME_KEY) {
      selected = 0;
    } else if (key == END_KEY) {
      selected = count - 1;
    } else if (key == '\r') {
      do_open = 1;
    } else if (key == MOUSE_EVENT) {
      /* Calculate panel geometry (must match file_browser_draw) */
      int panel_width = (editor.screen_columns * 70) / 100;
      if (panel_width < 40) panel_width = 40;
      if (panel_width > editor.screen_columns - 4) panel_width = editor.screen_columns - 4;
      int panel_left = (editor.screen_columns - panel_width) / 2;
      int panel_top = (editor.screen_rows - panel_height) / 2;

      /* Convert 1-indexed terminal coords to 0-indexed */
      int mx = last_mouse_event.column - 1;
      int my = last_mouse_event.row - 1;

      /* Handle scroll wheel */
      if (last_mouse_event.button_base == MOUSE_SCROLL_UP) {
        if (selected > 0) selected--;
      } else if (last_mouse_event.button_base == MOUSE_SCROLL_DOWN) {
        if (selected < count - 1) selected++;
      }
      /* Handle left click */
      else if (last_mouse_event.button_base == MOUSE_BUTTON_LEFT &&
               !last_mouse_event.is_release && !last_mouse_event.is_motion) {
        /* Check if click is within panel bounds */
        if (mx >= panel_left && mx < panel_left + panel_width &&
            my >= panel_top && my < panel_top + panel_height) {

          int panel_row = my - panel_top;

          /* Skip header (row 0) and help bar (last row) */
          if (panel_row > 0 && panel_row < panel_height - 1) {
            int clicked_item = scroll_offset + (panel_row - 1);

            if (clicked_item < count) {
              /* Detect double-click */
              struct timespec now;
              clock_gettime(CLOCK_MONOTONIC, &now);
              long ms_diff = (now.tv_sec - fb_last_click_time.tv_sec) * 1000 +
                             (now.tv_nsec - fb_last_click_time.tv_nsec) / 1000000;

              if (ms_diff < 400 && clicked_item == fb_last_click_item) {
                /* Double-click: open item */
                selected = clicked_item;
                do_open = 1;
              } else {
                /* Single click: select item */
                selected = clicked_item;
              }

              fb_last_click_time = now;
              fb_last_click_item = clicked_item;
            }
          }
        }
      }
    }

    /* Handle open action (Enter key or double-click) */
    if (do_open) {
      file_list_item *item = &items[selected];
      if (item->is_directory) {
        /* Navigate into directory */
        if (strcmp(item->actual_name, "..") == 0) {
          /* Go to parent */
          char *last_slash = strrchr(current_path, '/');
          if (last_slash && last_slash != current_path) {
            *last_slash = '\0';
          } else {
            strcpy(current_path, "/");
          }
        } else {
          size_t cur_len = strlen(current_path);
          size_t name_len = strlen(item->actual_name);
          if (cur_len + 1 + name_len < PATH_MAX) {
            strcat(current_path, "/");
            strcat(current_path, item->actual_name);
          }
        }
        selected = 0;
        scroll_offset = 0;
        fb_last_click_item = -1;  /* Reset double-click state on directory change */
      } else {
        /* Selected a file - return full path */
        size_t cur_len = strlen(current_path);
        size_t name_len = strlen(item->actual_name);
        size_t total_len = cur_len + 1 + name_len + 1;
        char *result = malloc(total_len);
        snprintf(result, total_len, "%s/%s", current_path, item->actual_name);
        file_list_free(items, count);
        return result;
      }
    }
    file_list_free(items, count);
  }
}

/* Clear current editor buffer */
void editor_clear_buffer(void) {
  /* Free all rows */
  for (int i = 0; i < editor.row_count; i++) {
    editor_free_row(&editor.row[i]);
  }
  free(editor.row);
  editor.row = NULL;
  editor.row_count = 0;

  /* Reset cursor */
  editor.cursor_x = 0;
  editor.cursor_y = 0;
  editor.render_x = 0;
  editor.row_offset = 0;
  editor.column_offset = 0;

  /* Clear filename */
  free(editor.filename);
  editor.filename = NULL;

  /* Clear selection */
  selection_clear();

  /* Reset undo state (history cleared implicitly with sqlite_sync_buffer) */
  editor.undo_group_id = 0;
  editor.undo_position = 0;
  editor.undo_memory_groups = 0;

  /* Clear dirty flag */
  editor.dirty = 0;

  /* Reset gutter */
  editor_update_gutter_width();
}

/* Open file browser and load selected file (Ctrl+O) */
void editor_open_file_browser(void) {
  /* Check for unsaved changes */
  if (editor.dirty) {
    char *response = editor_prompt("Save changes? (y/n/ESC to cancel): %s", NULL);
    if (response == NULL) {
      editor_set_status_message("Open cancelled");
      return;
    }
    if (response[0] == 'y' || response[0] == 'Y') {
      editor_save();
    }
    free(response);
  }

  char *filepath = editor_file_browser();
  if (filepath) {
    /* Clear current buffer and open new file */
    editor_clear_buffer();
    editor_open(filepath);
    free(filepath);
  } else {
    editor_set_status_message("Open cancelled");
  }
}

/* Get column position of first non-whitespace character in a row.
 * Returns 0 if row is NULL, empty, or contains only whitespace. */
int get_first_nonwhitespace_col(editor_row *row) {
  if (row == NULL || row->line_size == 0) return 0;
  int col = 0;
  while (col < row->line_size && (row->chars[col] == ' ' || row->chars[col] == '\t')) {
    col++;
  }
  /* If line is all whitespace, return 0 instead of end-of-line */
  if (col >= row->line_size) return 0;
  return col;
}

/* Move cursor in response to arrow keys.
 * Handles line wrapping and soft-wrap visual navigation. */
void editor_move_cursor(int key) {
  editor_row *row = (editor.cursor_y >= editor.row_count) ? NULL : &editor.row[editor.cursor_y];

  switch (key) {
    case ARROW_LEFT:
      if (editor.cursor_x != 0) {
        editor.cursor_x--;
      } else if (editor.cursor_y > 0) {
        editor.cursor_y--;
        editor.cursor_x = editor.row[editor.cursor_y].line_size;
      }
      break;
    case ARROW_RIGHT:
      if (row && editor.cursor_x < row->line_size) {
        editor.cursor_x++;
      } else if (row && editor.cursor_x == row->line_size) {
        editor.cursor_y++;
        editor.cursor_x = 0;
      }
      break;
    case ARROW_UP:
      if (editor.soft_wrap && editor.cursor_y < editor.row_count) {
        /* With soft wrap, move between wrap segments within same line */
        int available_width = editor.screen_columns - editor.gutter_width;
        if (available_width <= 0) break;

        editor_row *cur_row = &editor.row[editor.cursor_y];
        editor_calculate_wrap_breaks(cur_row, available_width);

        int current_rx = editor_row_cursor_to_render(cur_row, editor.cursor_x);
        int wrap_segment = editor_rx_to_wrap_segment(cur_row, current_rx);

        if (wrap_segment > 0) {
          /* Move up one wrap segment within same line */
          int segment_start = editor_wrap_segment_start(cur_row, wrap_segment);
          int offset_in_segment = current_rx - segment_start;

          int prev_segment_start = editor_wrap_segment_start(cur_row, wrap_segment - 1);
          int prev_segment_end = editor_wrap_segment_end(cur_row, wrap_segment - 1);

          int target_rx = prev_segment_start + offset_in_segment;
          if (target_rx > prev_segment_end) target_rx = prev_segment_end;

          editor.cursor_x = editor_row_render_to_cursor(cur_row, target_rx);
        } else {
          /* Already at first wrap segment, move to previous line */
          if (editor.cursor_y != 0) {
            editor.cursor_y--;
            editor_row *prev_row = &editor.row[editor.cursor_y];
            editor_calculate_wrap_breaks(prev_row, available_width);

            /* Move to last wrap segment of previous line */
            int total_segments = editor_row_visual_rows(prev_row);
            int last_segment = total_segments - 1;

            int segment_start = editor_wrap_segment_start(cur_row, 0);
            int offset_in_segment = current_rx - segment_start;

            int last_segment_start = editor_wrap_segment_start(prev_row, last_segment);
            int last_segment_end = editor_wrap_segment_end(prev_row, last_segment);

            int target_rx = last_segment_start + offset_in_segment;
            if (target_rx > last_segment_end) target_rx = last_segment_end;

            editor.cursor_x = editor_row_render_to_cursor(prev_row, target_rx);
          }
        }
      } else {
        /* Normal up movement without soft wrap */
        if (editor.cursor_y != 0) {
          editor.cursor_y--;
        }
      }
      break;
    case ARROW_DOWN:
      if (editor.soft_wrap && editor.cursor_y < editor.row_count) {
        /* With soft wrap, move between wrap segments within same line */
        int available_width = editor.screen_columns - editor.gutter_width;
        if (available_width <= 0) break;

        editor_row *cur_row = &editor.row[editor.cursor_y];
        editor_calculate_wrap_breaks(cur_row, available_width);

        int current_rx = editor_row_cursor_to_render(cur_row, editor.cursor_x);
        int wrap_segment = editor_rx_to_wrap_segment(cur_row, current_rx);
        int total_segments = editor_row_visual_rows(cur_row);

        if (wrap_segment < total_segments - 1) {
          /* Move down one wrap segment within same line */
          int segment_start = editor_wrap_segment_start(cur_row, wrap_segment);
          int offset_in_segment = current_rx - segment_start;

          int next_segment_start = editor_wrap_segment_start(cur_row, wrap_segment + 1);
          int next_segment_end = editor_wrap_segment_end(cur_row, wrap_segment + 1);

          int target_rx = next_segment_start + offset_in_segment;
          if (target_rx > next_segment_end) target_rx = next_segment_end;

          editor.cursor_x = editor_row_render_to_cursor(cur_row, target_rx);
        } else {
          /* Already at last wrap segment, move to next line */
          if (editor.cursor_y < editor.row_count) {
            editor.cursor_y++;
            if (editor.cursor_y < editor.row_count) {
              /* Move to first wrap segment of next line */
              editor_row *next_row = &editor.row[editor.cursor_y];
              editor_calculate_wrap_breaks(next_row, available_width);

              int segment_start = editor_wrap_segment_start(cur_row, wrap_segment);
              int offset_in_segment = current_rx - segment_start;

              int first_segment_end = editor_wrap_segment_end(next_row, 0);

              int target_rx = offset_in_segment;
              if (target_rx > first_segment_end) target_rx = first_segment_end;

              editor.cursor_x = editor_row_render_to_cursor(next_row, target_rx);
            } else {
              editor.cursor_x = 0;
            }
          }
        }
      } else {
        /* Normal down movement without soft wrap */
        if (editor.cursor_y < editor.row_count) {
          editor.cursor_y++;
        }
      }
      break;
  }

  row = (editor.cursor_y >= editor.row_count) ? NULL : &editor.row[editor.cursor_y];
  int rowlen = row ? row->line_size : 0;
  if (editor.cursor_x > rowlen) {
    editor.cursor_x = rowlen;
  }
}

/* Check if character is a word character (alphanumeric or underscore) */
int is_word_char(int c) {
  return isalnum(c) || c == '_';
}

/* Move cursor to the beginning of the previous word */
void editor_move_word_left() {
  if (editor.cursor_y >= editor.row_count) return;

  /* If at beginning of line, move to end of previous line */
  if (editor.cursor_x == 0) {
    if (editor.cursor_y > 0) {
      editor.cursor_y--;
      editor.cursor_x = editor.row[editor.cursor_y].line_size;
    }
    return;
  }

  editor_row *row = &editor.row[editor.cursor_y];
  int x = editor.cursor_x;

  /* Skip whitespace/separators going backward */
  while (x > 0 && !is_word_char(row->chars[x - 1])) {
    x--;
  }

  /* Skip word characters going backward */
  while (x > 0 && is_word_char(row->chars[x - 1])) {
    x--;
  }

  editor.cursor_x = x;
}

/* Move cursor to the end of the next word */
void editor_move_word_right() {
  if (editor.cursor_y >= editor.row_count) return;

  editor_row *row = &editor.row[editor.cursor_y];

  /* If at end of line, move to beginning of next line */
  if (editor.cursor_x >= row->line_size) {
    if (editor.cursor_y < editor.row_count - 1) {
      editor.cursor_y++;
      editor.cursor_x = 0;
    }
    return;
  }

  int x = editor.cursor_x;

  /* Skip word characters going forward */
  while (x < row->line_size && is_word_char(row->chars[x])) {
    x++;
  }

  /* Skip whitespace/separators going forward */
  while (x < row->line_size && !is_word_char(row->chars[x])) {
    x++;
  }

  editor.cursor_x = x;
}

/* Delete word backward (Ctrl+Backspace or Ctrl+W behavior) */
void editor_delete_word_backward() {
  if (editor.cursor_count > 0) {
    multicursor_delete_word_backward_all();
    return;
  }

  if (editor.cursor_y >= editor.row_count) return;

  /* If at beginning of line, join with previous line */
  if (editor.cursor_x == 0) {
    if (editor.cursor_y > 0) {
      editor_delete_char();
    }
    return;
  }

  editor_row *row = &editor.row[editor.cursor_y];
  int start_x = editor.cursor_x;
  int x = editor.cursor_x;

  /* Skip whitespace/separators going backward */
  while (x > 0 && !is_word_char(row->chars[x - 1])) {
    x--;
  }

  /* Skip word characters going backward */
  while (x > 0 && is_word_char(row->chars[x - 1])) {
    x--;
  }

  /* Delete characters from x to start_x */
  int chars_to_delete = start_x - x;
  editor.cursor_x = x;
  for (int i = 0; i < chars_to_delete; i++) {
    editor_row_delete_char(row, editor.cursor_x);
  }
  editor.dirty++;
}

/* Delete word forward (Ctrl+Delete behavior) */
void editor_delete_word_forward() {
  if (editor.cursor_count > 0) {
    multicursor_delete_word_forward_all();
    return;
  }

  if (editor.cursor_y >= editor.row_count) return;

  editor_row *row = &editor.row[editor.cursor_y];

  /* If at end of line, join with next line */
  if (editor.cursor_x >= row->line_size) {
    if (editor.cursor_y < editor.row_count - 1) {
      editor_move_cursor(ARROW_RIGHT);
      editor_delete_char();
    }
    return;
  }

  int x = editor.cursor_x;

  /* Skip word characters going forward */
  while (x < row->line_size && is_word_char(row->chars[x])) {
    x++;
  }

  /* Skip whitespace/separators going forward */
  while (x < row->line_size && !is_word_char(row->chars[x])) {
    x++;
  }

  /* Delete characters from cursor_x to x */
  int chars_to_delete = x - editor.cursor_x;
  for (int i = 0; i < chars_to_delete; i++) {
    editor_row_delete_char(row, editor.cursor_x);
  }
  editor.dirty++;
}

/* Get the matching bracket character for a given bracket */
char get_matching_bracket(char c) {
  switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    default: return '\0';
  }
}

/* Check if character is an opening bracket */
int is_opening_bracket(char c) {
  return c == '(' || c == '[' || c == '{';
}

/* Reset bracket match state */
void editor_reset_bracket_match() {
  editor.bracket_match_row = -1;
  editor.bracket_match_col = -1;
  editor.bracket_open_row = -1;
  editor.bracket_open_col = -1;
  editor.bracket_open_len = 0;
  editor.bracket_close_row = -1;
  editor.bracket_close_col = -1;
  editor.bracket_close_len = 0;
}

static int editor_search_comment_forward(const char *needle, int needle_len, int start_row, int start_col, int *out_row, int *out_col) {
  if (needle_len <= 0) return 0;
  for (int r = start_row; r < editor.row_count; r++) {
    editor_row *row = &editor.row[r];
    int sc = (r == start_row) ? start_col : 0;
    for (int c = sc; c + needle_len <= row->line_size; c++) {
      if (!strncmp(&row->chars[c], needle, needle_len)) {
        *out_row = r;
        *out_col = c;
        return 1;
      }
    }
  }
  return 0;
}

static int editor_search_comment_backward(const char *needle, int needle_len, int start_row, int start_col, int *out_row, int *out_col) {
  if (needle_len <= 0) return 0;
  for (int r = start_row; r >= 0; r--) {
    editor_row *row = &editor.row[r];
    int sc = (r == start_row) ? start_col : row->line_size - 1;
    for (int c = sc; c - needle_len + 1 >= 0; c--) {
      if (!strncmp(&row->chars[c - needle_len + 1], needle, needle_len)) {
        *out_row = r;
        *out_col = c - needle_len + 1;
        return 1;
      }
    }
  }
  return 0;
}

/* Internal: find match starting at specific position (bracket or comment delimiter). */
static int editor_match_from(int start_row, int start_col) {
  editor_reset_bracket_match();

  if (start_row < 0 || start_row >= editor.row_count) return 0;
  editor_row *row = &editor.row[start_row];
  if (start_col < 0 || start_col >= row->line_size) return 0;

  char current = row->chars[start_col];
  char match = get_matching_bracket(current);

  int direction = is_opening_bracket(current) ? 1 : -1;
  int depth = 1;
  int search_row = start_row;
  int search_col = start_col + direction;
  int in_string = 0;
  char string_delim = '\0';
  int in_multiline_comment = 0;

  char *ml_start = editor.syntax ? editor.syntax->multiline_comment_start : NULL;
  char *ml_end = editor.syntax ? editor.syntax->multiline_comment_end : NULL;
  int ml_start_len = ml_start ? (int)strlen(ml_start) : 0;
  int ml_end_len = ml_end ? (int)strlen(ml_end) : 0;

  /* Handle multiline comment delimiters as matchable pairs */
  if (ml_start_len && ml_end_len &&
      start_col + ml_start_len <= row->line_size &&
      !strncmp(&row->chars[start_col], ml_start, ml_start_len)) {
    int mr, mc;
    if (editor_search_comment_forward(ml_end, ml_end_len, start_row, start_col + ml_start_len, &mr, &mc)) {
      editor.bracket_match_row = mr;
      editor.bracket_match_col = mc;
      editor.bracket_open_row = start_row;
      editor.bracket_open_col = start_col;
      editor.bracket_open_len = ml_start_len;
      editor.bracket_close_row = mr;
      editor.bracket_close_col = mc;
      editor.bracket_close_len = ml_end_len;
      return 1;
    }
    return 0;
  }
  if (ml_start_len && ml_end_len &&
      start_col + ml_end_len <= row->line_size &&
      !strncmp(&row->chars[start_col], ml_end, ml_end_len)) {
    int mr, mc;
    if (editor_search_comment_backward(ml_start, ml_start_len, start_row, start_col - 1, &mr, &mc)) {
      editor.bracket_match_row = mr;
      editor.bracket_match_col = mc;
      editor.bracket_open_row = mr;
      editor.bracket_open_col = mc;
      editor.bracket_open_len = ml_start_len;
      editor.bracket_close_row = start_row;
      editor.bracket_close_col = start_col;
      editor.bracket_close_len = ml_end_len;
      return 1;
    }
    return 0;
  }

  if (match == '\0') return 0;

  while (depth > 0) {
    if (direction > 0) {
      while (search_row < editor.row_count) {
        editor_row *r = &editor.row[search_row];
        while (search_col < r->line_size) {
          char c = r->chars[search_col];

          if (!in_multiline_comment) {
            if (in_string) {
              if (c == string_delim) {
                int backslashes = 0;
                int k = search_col - 1;
                while (k >= 0 && r->chars[k] == '\\') {
                  backslashes++;
                  k--;
                }
                if (backslashes % 2 == 0) {
                  in_string = 0;
                  string_delim = '\0';
                }
              }
              search_col++;
              continue;
            } else if (c == '"' || c == '\'') {
              in_string = 1;
              string_delim = c;
              search_col++;
              continue;
            }
          }

          if (ml_start_len && ml_end_len && !in_string) {
            if (!in_multiline_comment &&
                search_col + ml_start_len <= r->line_size &&
                strncmp(&r->chars[search_col], ml_start, ml_start_len) == 0) {
              in_multiline_comment = 1;
              search_col += ml_start_len;
              continue;
            } else if (in_multiline_comment &&
                       search_col + ml_end_len <= r->line_size &&
                       strncmp(&r->chars[search_col], ml_end, ml_end_len) == 0) {
              in_multiline_comment = 0;
              search_col += ml_end_len;
              continue;
            }
            if (in_multiline_comment) {
              search_col++;
              continue;
            }
          }

          if (c == current) {
            depth++;
          } else if (c == match) {
            depth--;
            if (depth == 0) {
              editor.bracket_match_row = search_row;
              editor.bracket_match_col = search_col;
              if (direction > 0) {
                editor.bracket_open_row = start_row;
                editor.bracket_open_col = start_col;
                editor.bracket_open_len = 1;
                editor.bracket_close_row = search_row;
                editor.bracket_close_col = search_col;
                editor.bracket_close_len = 1;
              } else {
                editor.bracket_open_row = search_row;
                editor.bracket_open_col = search_col;
                editor.bracket_open_len = 1;
                editor.bracket_close_row = start_row;
                editor.bracket_close_col = start_col;
                editor.bracket_close_len = 1;
              }
              return 1;
            }
          }
          search_col++;
        }
        search_row++;
        search_col = 0;
      }
      break;
    } else {
      while (search_row >= 0) {
        editor_row *r = &editor.row[search_row];
        if (search_col < 0) search_col = r->line_size - 1;
        while (search_col >= 0) {
          char c = r->chars[search_col];

          if (!in_multiline_comment) {
            if (in_string) {
              if (c == string_delim) {
                int backslashes = 0;
                int k = search_col - 1;
                while (k >= 0 && r->chars[k] == '\\') {
                  backslashes++;
                  k--;
                }
                if (backslashes % 2 == 0) {
                  in_string = 0;
                  string_delim = '\0';
                }
              }
              search_col--;
              continue;
            } else if (c == '"' || c == '\'') {
              in_string = 1;
              string_delim = c;
              search_col--;
              continue;
            }
          }

          if (ml_start_len && ml_end_len && !in_string) {
            if (!in_multiline_comment &&
                search_col - ml_end_len + 1 >= 0 &&
                strncmp(&r->chars[search_col - ml_end_len + 1], ml_end, ml_end_len) == 0) {
              in_multiline_comment = 1;
              search_col -= ml_end_len;
              continue;
            } else if (in_multiline_comment &&
                       search_col - ml_start_len + 1 >= 0 &&
                       strncmp(&r->chars[search_col - ml_start_len + 1], ml_start, ml_start_len) == 0) {
              in_multiline_comment = 0;
              search_col -= ml_start_len;
              continue;
            }
            if (in_multiline_comment) {
              search_col--;
              continue;
            }
          }

          if (c == current) {
            depth++;
          } else if (c == match) {
            depth--;
            if (depth == 0) {
              editor.bracket_match_row = search_row;
              editor.bracket_match_col = search_col;
              if (direction > 0) {
                editor.bracket_open_row = start_row;
                editor.bracket_open_col = start_col;
                editor.bracket_open_len = 1;
                editor.bracket_close_row = search_row;
                editor.bracket_close_col = search_col;
                editor.bracket_close_len = 1;
              } else {
                editor.bracket_open_row = search_row;
                editor.bracket_open_col = search_col;
                editor.bracket_open_len = 1;
                editor.bracket_close_row = start_row;
                editor.bracket_close_col = start_col;
                editor.bracket_close_len = 1;
              }
              return 1;
            }
          }
          search_col--;
        }
        search_row--;
        if (search_row >= 0) {
          search_col = editor.row[search_row].line_size - 1;
        }
      }
      break;
    }
  }

  return 0;
}

/* Check if cursor position is inside a multiline comment.
 * If so, sets out_start_row/col to the opening delimiter position.
 * Returns 1 if inside comment, 0 otherwise. */
static int editor_cursor_in_multiline_comment(int *out_start_row, int *out_start_col) {
  char *ml_start = editor.syntax ? editor.syntax->multiline_comment_start : NULL;
  char *ml_end = editor.syntax ? editor.syntax->multiline_comment_end : NULL;
  if (!ml_start || !ml_end) return 0;
  int ml_start_len = (int)strlen(ml_start);
  int ml_end_len = (int)strlen(ml_end);
  if (ml_start_len == 0 || ml_end_len == 0) return 0;

  /* Scan forward from start of file to cursor, tracking comment state */
  int in_comment = 0;
  int comment_start_row = -1, comment_start_col = -1;
  int in_string = 0;
  char string_delim = '\0';

  for (int r = 0; r <= editor.cursor_y && r < editor.row_count; r++) {
    editor_row *row = &editor.row[r];
    int end_col = (r == editor.cursor_y) ? editor.cursor_x : row->line_size;
    for (int c = 0; c < end_col && c < row->line_size; c++) {
      char ch = row->chars[c];

      if (!in_comment) {
        /* Track string state to avoid matching delimiters inside strings */
        if (in_string) {
          if (ch == string_delim) {
            int backslashes = 0;
            for (int k = c - 1; k >= 0 && row->chars[k] == '\\'; k--) backslashes++;
            if (backslashes % 2 == 0) in_string = 0;
          }
          continue;
        }
        if (ch == '"' || ch == '\'') {
          in_string = 1;
          string_delim = ch;
          continue;
        }

        /* Check for comment start */
        if (c + ml_start_len <= row->line_size &&
            strncmp(&row->chars[c], ml_start, ml_start_len) == 0) {
          in_comment = 1;
          comment_start_row = r;
          comment_start_col = c;
          c += ml_start_len - 1;  /* -1 because loop will increment */
        }
      } else {
        /* Check for comment end */
        if (c + ml_end_len <= row->line_size &&
            strncmp(&row->chars[c], ml_end, ml_end_len) == 0) {
          in_comment = 0;
          comment_start_row = -1;
          comment_start_col = -1;
          c += ml_end_len - 1;
        }
      }
    }
  }

  if (in_comment && comment_start_row >= 0) {
    *out_start_row = comment_start_row;
    *out_start_col = comment_start_col;
    return 1;
  }
  return 0;
}

/* Find matching bracket position.
 * Scans across lines and skips over string literals and multi-line comments
 * based on current syntax rules. Sets editor.bracket_match_row/col.
 * Returns 1 if match found, 0 otherwise.
 * Additionally, when cursor is inside a bracketed region, match the nearest enclosing pair.
 * When cursor is inside a multiline comment, highlights the comment delimiters. */
int editor_find_matching_bracket() {
  editor_reset_bracket_match();

  if (editor.cursor_y >= editor.row_count) return 0;

  editor_row *row = &editor.row[editor.cursor_y];

  /* First check if cursor is inside a multiline comment */
  int comment_start_row, comment_start_col;
  if (editor_cursor_in_multiline_comment(&comment_start_row, &comment_start_col)) {
    /* We're inside a comment - match the comment delimiters */
    return editor_match_from(comment_start_row, comment_start_col);
  }

  if (editor.cursor_x >= row->line_size) return 0;

  /* Try matching at the cursor position (bracket or comment delimiter) */
  if (editor_match_from(editor.cursor_y, editor.cursor_x)) {
    return 1;
  }

  /* Otherwise, find the nearest enclosing opening bracket before the cursor.
   * We scan backwards, tracking string/comment state properly. */
  char *ml_start = editor.syntax ? editor.syntax->multiline_comment_start : NULL;
  char *ml_end = editor.syntax ? editor.syntax->multiline_comment_end : NULL;
  int ml_start_len = ml_start ? (int)strlen(ml_start) : 0;
  int ml_end_len = ml_end ? (int)strlen(ml_end) : 0;

  /* Track depth of each bracket type to find enclosing bracket */
  int paren_depth = 0;   /* () */
  int bracket_depth = 0; /* [] */
  int brace_depth = 0;   /* {} */
  int in_string = 0;
  char string_delim = '\0';
  int in_multiline_comment = 0;

  for (int sr = editor.cursor_y; sr >= 0; sr--) {
    editor_row *r = &editor.row[sr];
    int sc = (sr == editor.cursor_y) ? editor.cursor_x - 1 : r->line_size - 1;
    for (; sc >= 0; sc--) {
      char c = r->chars[sc];

      /* Handle multiline comment tracking (backwards: end-delim enters, start-delim exits) */
      if (ml_start_len && ml_end_len && !in_string) {
        if (!in_multiline_comment &&
            sc - ml_end_len + 1 >= 0 &&
            strncmp(&r->chars[sc - ml_end_len + 1], ml_end, ml_end_len) == 0) {
          in_multiline_comment = 1;
          sc -= ml_end_len - 1;
          continue;
        } else if (in_multiline_comment &&
                   sc - ml_start_len + 1 >= 0 &&
                   strncmp(&r->chars[sc - ml_start_len + 1], ml_start, ml_start_len) == 0) {
          in_multiline_comment = 0;
          sc -= ml_start_len - 1;
          continue;
        }
      }

      /* Skip content inside comments */
      if (in_multiline_comment) {
        continue;
      }

      /* Handle string tracking */
      if (in_string) {
        if (c == string_delim) {
          int backslashes = 0;
          for (int k = sc - 1; k >= 0 && r->chars[k] == '\\'; k--) backslashes++;
          if (backslashes % 2 == 0) {
            in_string = 0;
            string_delim = '\0';
          }
        }
        continue;
      }
      if (c == '"' || c == '\'') {
        in_string = 1;
        string_delim = c;
        continue;
      }

      /* Track bracket depth - closing brackets increase depth (going backwards) */
      if (c == ')') paren_depth++;
      else if (c == ']') bracket_depth++;
      else if (c == '}') brace_depth++;
      else if (c == '(') {
        if (paren_depth > 0) paren_depth--;
        else {
          /* Found enclosing opening paren */
          if (editor_match_from(sr, sc)) return 1;
        }
      }
      else if (c == '[') {
        if (bracket_depth > 0) bracket_depth--;
        else {
          /* Found enclosing opening bracket */
          if (editor_match_from(sr, sc)) return 1;
        }
      }
      else if (c == '{') {
        if (brace_depth > 0) brace_depth--;
        else {
          /* Found enclosing opening brace */
          if (editor_match_from(sr, sc)) return 1;
        }
      }
    }
  }

  return 0;
}

/* Jump cursor to matching bracket */
void editor_jump_to_matching_bracket() {
  if (editor_find_matching_bracket()) {
    editor.cursor_y = editor.bracket_match_row;
    editor.cursor_x = editor.bracket_match_col;
  }
}

/* Adjust scroll speed based on time between consecutive scroll events.
 * Faster scrolling = higher multiplier (tactile scrolling). */
void editor_update_scroll_speed(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* Calculate time difference in microseconds */
  long time_diff_us = (now.tv_sec - editor.last_scroll_time.tv_sec) * 1000000 +
                      (now.tv_nsec - editor.last_scroll_time.tv_nsec) / 1000;

  /* Adjust speed based on timing */
  if (time_diff_us < 80000) {         /* <80ms: accelerate */
    if (editor.scroll_speed < 15) editor.scroll_speed++;
  } else if (time_diff_us > 150000) { /* >150ms: reset */
    editor.scroll_speed = 1;
  }
  /* 80-150ms: maintain current speed */

  editor.last_scroll_time = now;
}

/* Convert screen coordinates to file position and handle mouse action */
/* Track if menu was just opened to prevent immediate toggle-off */
static int menu_just_opened = 0;

/* Handle click on menu bar (row 0 when menu visible) */
static void menu_handle_bar_click(int x) {
  if (x < 0) return;  /* Sanity check */

  for (int i = 0; i < MENU_COUNT; i++) {
    if (!menus[i].title) continue;  /* Safety check */
    int start = menus[i].x_position;
    int end = start + (int)strlen(menus[i].title) + 2;
    if (x >= start && x < end) {
      if (editor.menu_open == i && !menu_just_opened) {
        editor.menu_open = -1;  /* Toggle off (only if not just opened) */
      } else {
        editor.menu_open = i;
        editor.menu_selected_item = 0;
        menu_just_opened = 1;  /* Mark as just opened */
        /* Skip separators for initial selection */
        while (editor.menu_selected_item < menus[i].item_count &&
               menus[i].items[editor.menu_selected_item].label == NULL) {
          editor.menu_selected_item++;
        }
      }
      return;
    }
  }
  editor.menu_open = -1;  /* Click outside menus closes */
  menu_just_opened = 0;
}

/* Handle click on dropdown menu item */
static int menu_handle_dropdown_click(int x, int y) {
  if (editor.menu_open < 0 || editor.menu_open >= MENU_COUNT) return 0;

  menu_def *menu = &menus[editor.menu_open];
  int menu_x = menu->x_position;

  /* Calculate width if not set yet */
  int menu_width = menu->width;
  if (menu_width <= 0) {
    menu_width = menu_calculate_width(menu);
  }

  /* Ensure dropdown position matches render logic */
  if (menu_x + menu_width > editor.screen_columns) {
    menu_x = editor.screen_columns - menu_width;
    if (menu_x < 0) menu_x = 0;
  }

  /* Check if click is within dropdown bounds */
  /* Dropdown starts at row 1 (0-indexed), below menu bar */
  int item_idx = y - 1;  /* y=1 -> item 0, y=2 -> item 1, etc. */

  if (x >= menu_x && x < menu_x + menu_width &&
      item_idx >= 0 && item_idx < menu->item_count) {
    menu_item *item = &menu->items[item_idx];
    if (item->label != NULL) {  /* Not a separator */
      editor.menu_open = -1;
      if (item->action) {
        item->action();
      }
      return 1;
    }
  }
  return 0;
}

/* Execute currently selected menu item */
static void menu_execute_selected(void) {
  if (editor.menu_open < 0) return;

  menu_def *menu = &menus[editor.menu_open];
  if (editor.menu_selected_item >= 0 && editor.menu_selected_item < menu->item_count) {
    menu_item *item = &menu->items[editor.menu_selected_item];
    if (item->label != NULL && item->action) {
      editor.menu_open = -1;
      item->action();
    }
  }
}

/* Move selection in dropdown menu */
static void menu_move_selection(int direction) {
  if (editor.menu_open < 0) return;

  menu_def *menu = &menus[editor.menu_open];
  int new_sel = editor.menu_selected_item + direction;

  /* Skip separators */
  while (new_sel >= 0 && new_sel < menu->item_count &&
         menu->items[new_sel].label == NULL) {
    new_sel += direction;
  }

  /* Bounds check */
  if (new_sel >= 0 && new_sel < menu->item_count) {
    editor.menu_selected_item = new_sel;
  }
}

/* Switch to previous/next menu */
static void menu_switch(int direction) {
  if (editor.menu_open < 0) return;

  int new_menu = editor.menu_open + direction;
  if (new_menu < 0) new_menu = MENU_COUNT - 1;
  if (new_menu >= MENU_COUNT) new_menu = 0;

  editor.menu_open = new_menu;
  editor.menu_selected_item = 0;
  /* Skip separators for initial selection */
  while (editor.menu_selected_item < menus[new_menu].item_count &&
         menus[new_menu].items[editor.menu_selected_item].label == NULL) {
    editor.menu_selected_item++;
  }
}

void editor_handle_mouse_event() {
  int screen_x = last_mouse_event.column - 1;
  int screen_y = last_mouse_event.row - 1;

  /* Handle menu bar interactions (when menu visible) */
  if (editor.menu_bar_visible) {
    /* When a menu is open, consume all mouse events except:
     * - Press on menu bar (to switch menus)
     * - Press on dropdown item (to select)
     * - Press outside (to close)
     */
    if (editor.menu_open >= 0) {
      /* Ignore releases and motion entirely when menu is open */
      if (last_mouse_event.is_release || last_mouse_event.is_motion) {
        /* Clear the "just opened" flag on release so next click can toggle */
        if (last_mouse_event.is_release) {
          menu_just_opened = 0;
        }
        return;
      }

      /* Only process left button presses */
      if (last_mouse_event.button_base == MOUSE_BUTTON_LEFT) {
        /* Click on menu bar - switch menus or close */
        if (screen_y == 0) {
          menu_handle_bar_click(screen_x);
          return;
        }

        /* Click on dropdown area */
        if (menu_handle_dropdown_click(screen_x, screen_y)) {
          return;
        }

        /* Click outside - close menu */
        editor.menu_open = -1;
        return;
      }

      /* Ignore other button presses when menu open */
      return;
    }

    /* No menu open - check for click on menu bar to open one */
    if (screen_y == 0 && last_mouse_event.button_base == MOUSE_BUTTON_LEFT &&
        !last_mouse_event.is_motion && !last_mouse_event.is_release) {
      menu_handle_bar_click(screen_x);
      return;
    }

    /* Adjust screen_y for text area (menu bar takes row 0) */
    screen_y--;
  }

  /* Handle scroll wheel with tactile (velocity-based) speed */
  if (last_mouse_event.button_base == MOUSE_SCROLL_UP) {
    editor_update_scroll_speed();
    for (int i = 0; i < editor.scroll_speed; i++) {
      editor_move_cursor(ARROW_UP);
    }
    return;
  }
  if (last_mouse_event.button_base == MOUSE_SCROLL_DOWN) {
    editor_update_scroll_speed();
    for (int i = 0; i < editor.scroll_speed; i++) {
      editor_move_cursor(ARROW_DOWN);
    }
    return;
  }

  /* Only handle left button (button_base 0) */
  if (last_mouse_event.button_base != MOUSE_BUTTON_LEFT) return;

  /* Calculate message bar position (after status bar) */
  int message_bar_row = editor.screen_rows + 1;

  /* Check for click on message bar (last row) - copy message to clipboard */
  if (screen_y == message_bar_row && !last_mouse_event.is_motion) {
    if (!last_mouse_event.is_release && strlen(editor.status_message) > 0) {
      clipboard_store(editor.status_message, 1);
      clipboard_sync_to_system(editor.status_message);
      editor_set_status_message("Message copied to clipboard");
    }
    return;
  }

  /* Ignore clicks in gutter area */
  if (screen_x < editor.gutter_width) return;

  /* Ignore clicks on status bar or message bar */
  if (screen_y >= editor.screen_rows) return;

  /* Convert screen row to file row */
  int visual_row = screen_y + editor.row_offset;
  int file_row, wrap_row;
  if (!editor_visual_to_logical(visual_row, &file_row, &wrap_row)) {
    /* Click past end of file - go to last line */
    file_row = editor.row_count > 0 ? editor.row_count - 1 : 0;
    wrap_row = 0;
  }

  /* Clamp to valid row range */
  if (file_row >= editor.row_count) {
    file_row = editor.row_count > 0 ? editor.row_count - 1 : 0;
  }

  /* Calculate render_x from screen position */
  int render_x = screen_x - editor.gutter_width;

  /* For soft wrap, adjust based on wrap segment */
  if (editor.soft_wrap && file_row < editor.row_count) {
    editor_row *row = &editor.row[file_row];
    int segment_start = editor_wrap_segment_start(row, wrap_row);
    render_x += segment_start;
  } else {
    render_x += editor.column_offset;
  }

  /* Convert render_x to cursor_x (accounting for tabs) */
  int cursor_x = 0;
  if (file_row < editor.row_count) {
    cursor_x = editor_row_render_to_cursor(&editor.row[file_row], render_x);
    /* Clamp to line length */
    if (cursor_x > editor.row[file_row].line_size) {
      cursor_x = editor.row[file_row].line_size;
    }
  }

  /* Modifier-assisted multi-cursor placement (Ctrl/Alt + click) */
  if (!last_mouse_event.is_motion &&
      !last_mouse_event.is_release &&
      (last_mouse_event.modifiers & (MOUSE_MOD_CTRL | MOUSE_MOD_ALT))) {
    if (multicursor_add(file_row, cursor_x)) {
      selection_clear();
      editor_set_status_message("Added cursor at line %d, col %d (total: %zu)",
                                file_row + 1, cursor_x + 1, editor.cursor_count + 1);
    } else {
      editor_set_status_message("Cursor already exists here");
    }
    return;
  }

  /* Handle mouse motion (dragging) - extend selection */
  if (last_mouse_event.is_motion) {
    if (editor.selection.active) {
      editor.cursor_x = cursor_x;
      editor.cursor_y = file_row;
      selection_extend();
    }
    return;
  }

  /* Ignore button releases */
  if (last_mouse_event.is_release) return;

  /* Check for Shift+Click to extend selection */
  if (last_mouse_event.modifiers & MOUSE_MOD_SHIFT) {
    if (!editor.selection.active) {
      /* Start selection from current cursor position */
      selection_start();
    }
    /* Move cursor and extend selection to click position */
    editor.cursor_x = cursor_x;
    editor.cursor_y = file_row;
    selection_extend();
    return;
  }

  /* Handle multi-click detection (double/triple click) */
  selection_detect_multi_click(file_row, cursor_x);

  if (editor.selection.click_count == 2) {
    /* Double-click: select word */
    editor.cursor_x = cursor_x;
    editor.cursor_y = file_row;
    selection_select_word(file_row, cursor_x);
  } else if (editor.selection.click_count >= 3) {
    /* Triple-click: select line */
    editor.cursor_x = cursor_x;
    editor.cursor_y = file_row;
    selection_select_line(file_row);
  } else {
    /* Single click: start drag selection */
    editor.cursor_x = cursor_x;
    editor.cursor_y = file_row;
    selection_start();
  }
}

/* Handle a single keypress. Maps keys to editor commands
 * including editing, navigation, search, and quit. */
void editor_process_keypress() {
  static int quit_times = MITER_QUIT_TIMES;

  int key = editor_read_key();

  /* No input available (timeout) - return immediately */
  if (key == -1) return;

  /* Check for menu quit request from menu action */
  if (menu_quit_requested) {
    menu_quit_requested = 0;
    if (editor.dirty) {
      editor_set_status_message("Save first (Ctrl+S) or Ctrl+Q 3x to quit");
    } else {
      write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN, ESCAPE_CLEAR_SCREEN_LEN);
      write(STDOUT_FILENO, ESCAPE_CURSOR_HOME, ESCAPE_CURSOR_HOME_LEN);
      /* removed */
      exit(0);
    }
    return;
  }

  /* Handle menu keyboard navigation when a menu is open */
  if (editor.menu_open >= 0) {
    switch (key) {
      case CHAR_ESCAPE:
        editor.menu_open = -1;
        return;
      case ARROW_UP:
        menu_move_selection(-1);
        return;
      case ARROW_DOWN:
        menu_move_selection(1);
        return;
      case ARROW_LEFT:
        menu_switch(-1);
        return;
      case ARROW_RIGHT:
        menu_switch(1);
        return;
      case '\r':
        menu_execute_selected();
        return;
      case MOUSE_EVENT:
        /* Let mouse events be handled below - don't close menu */
        break;
      default:
        /* Close menu on any other key */
        editor.menu_open = -1;
        /* Fall through to normal key processing */
        break;
    }
  }

  /* F10 opens/closes the menu bar */
  if (key == F10_KEY && editor.menu_bar_visible) {
    if (editor.menu_open >= 0) {
      editor.menu_open = -1;
    } else {
      editor.menu_open = 0;
      editor.menu_selected_item = 0;
    }
    return;
  }

  /* Reset Smart Home toggle state for all keys except Home */
  if (key != HOME_KEY) {
    editor.last_key_was_home = 0;
  }

  switch (key) {
    case '\r':
      editor_insert_newline();
      break;

    case CTRL_KEY('q'):
      if (editor.dirty && quit_times > 0) {
        editor_set_status_message("You have unsaved changes. Save with Ctrl-S, "
          "or press Ctrl-Q %d more times to quit anyway.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN, ESCAPE_CLEAR_SCREEN_LEN);
      write(STDOUT_FILENO, ESCAPE_CURSOR_HOME, ESCAPE_CURSOR_HOME_LEN);
      /* removed */
      exit(0);
      break;

    case CTRL_KEY('s'):
      editor_save();
      break;

    case CTRL_KEY('o'):
      editor_open_file_browser();
      break;

    case HOME_KEY:
      selection_clear();
      {
        /* Smart Home: toggle between first non-whitespace and column 0 */
        editor_row *row = (editor.cursor_y < editor.row_count) ?
                          &editor.row[editor.cursor_y] : NULL;
        int first_nonws = get_first_nonwhitespace_col(row);

        if (editor.last_key_was_home) {
          /* Second press: toggle to the other position */
          if (editor.cursor_x == 0) {
            editor.cursor_x = first_nonws;
          } else {
            editor.cursor_x = 0;
          }
        } else {
          /* First press: go to first non-whitespace, or column 0 if already there */
          if (editor.cursor_x == first_nonws || first_nonws == 0) {
            editor.cursor_x = 0;
          } else {
            editor.cursor_x = first_nonws;
          }
        }
        editor.last_key_was_home = 1;

        /* Align secondary cursors to same home target */
        if (editor.cursor_count > 0) {
          int use_first_nonws = (editor.cursor_x != 0);
          multicursor_apply_home_position(use_first_nonws);
        }
      }
      break;

    case END_KEY:
      selection_clear();
      if (editor.cursor_y < editor.row_count)
        editor.cursor_x = editor.row[editor.cursor_y].line_size;
      if (editor.cursor_count > 0) {
        multicursor_apply_end_position();
      }
      break;

    case CTRL_KEY('f'):
      editor_find();
      break;

    case CTRL_KEY('a'):
      selection_select_all();
      break;

    case CTRL_KEY('g'):
      editor_jump_to_line();
      break;

    case ALT_T:
      theme_cycle();
      break;

    case ALT_L:
      editor_toggle_line_numbers();
      break;

    case ALT_Q:
      editor_reflow_paragraph();
      break;

    case ALT_J:
      editor_join_paragraph();
      break;

    case ALT_S:
      /* removed */
      break;

    case ALT_R:
      /* SQL replace removed - Miter doesn't have SQLite */
      break;

    case ALT_N:
      /* Snippet search removed - Miter doesn't have SQLite */
      break;

    case ALT_W:
      editor_toggle_soft_wrap();
      break;

    case ALT_Z:
      editor_toggle_center_scroll();
      break;

    case ALT_OPEN_BRACKET:
      editor_skip_opening_pair();
      break;

    case ALT_CLOSE_BRACKET:
      editor_skip_closing_pair();
      break;

    case ALT_M:
      /* Toggle menu bar visibility */
      editor.menu_bar_visible = !editor.menu_bar_visible;
      editor.menu_open = -1;  /* Close any open menu */
      /* Recalculate screen rows */
      editor_handle_resize();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (key == DEL_KEY) editor_move_cursor(ARROW_RIGHT);
      editor_delete_char();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int original_line = editor.cursor_y;
        selection_clear();
        if (key == PAGE_UP) {
          editor.cursor_y = editor.row_offset;
        } else if (key == PAGE_DOWN) {
          editor.cursor_y = editor.row_offset + editor.screen_rows - 1;
          if (editor.cursor_y > editor.row_count) editor.cursor_y = editor.row_count;
        }

        int times = editor.screen_rows;
        while (times--)
          editor_move_cursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);

        if (editor.cursor_count > 0) {
          int delta_rows = editor.cursor_y - original_line;
          multicursor_apply_vertical_delta(delta_rows);
        }
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      selection_clear();
      editor_move_cursor(key);
      if (editor.cursor_count > 0) {
        multicursor_move_all(key);
      }
      break;

    /* Shift+Arrow: extend selection while moving cursor */
    case SHIFT_ARROW_UP:
      if (!editor.selection.active) selection_start();
      editor_move_cursor(ARROW_UP);
      selection_extend();
      break;
    case SHIFT_ARROW_DOWN:
      if (!editor.selection.active) selection_start();
      editor_move_cursor(ARROW_DOWN);
      selection_extend();
      break;
    case SHIFT_ARROW_LEFT:
      if (!editor.selection.active) selection_start();
      editor_move_cursor(ARROW_LEFT);
      selection_extend();
      break;
    case SHIFT_ARROW_RIGHT:
      if (!editor.selection.active) selection_start();
      editor_move_cursor(ARROW_RIGHT);
      selection_extend();
      break;
    case SHIFT_HOME:
      if (!editor.selection.active) selection_start();
      editor.cursor_x = 0;
      selection_extend();
      break;
    case SHIFT_END:
      if (!editor.selection.active) selection_start();
      if (editor.cursor_y < editor.row_count)
        editor.cursor_x = editor.row[editor.cursor_y].line_size;
      selection_extend();
      break;

    /* Word navigation (Ctrl+Arrow) */
    case CTRL_ARROW_LEFT:
      selection_clear();
      editor_move_word_left();
      if (editor.cursor_count > 0) {
        multicursor_move_word_left_all();
      }
      break;
    case CTRL_ARROW_RIGHT:
      selection_clear();
      editor_move_word_right();
      if (editor.cursor_count > 0) {
        multicursor_move_word_right_all();
      }
      break;

    /* Word deletion */
    case CTRL_KEY('w'):  /* Ctrl+W: delete word backward (Unix standard) */
      selection_clear();
      editor_delete_word_backward();
      break;
    case CTRL_DELETE:    /* Ctrl+Delete: delete word forward */
      selection_clear();
      editor_delete_word_forward();
      break;

    /* Clipboard operations */
    case CTRL_KEY('c'):
      editor_copy();
      break;
    case CTRL_KEY('x'):
      editor_cut();
      break;
    case CTRL_KEY('v'):
      editor_paste();
      break;

    /* Undo/Redo operations */
    case CTRL_KEY('z'):
      editor_undo();
      break;
    case CTRL_KEY('y'):
      editor_redo();
      break;

    /* Line manipulation operations */
    case CTRL_KEY('d'):
      editor_duplicate_line();
      break;
    case CTRL_KEY('k'):
      editor_delete_line();
      break;
    case CTRL_KEY('j'):
      editor_join_lines();
      break;
    case ALT_SHIFT_UP:
      editor_move_line_up();
      break;
    case ALT_SHIFT_DOWN:
      editor_move_line_down();
      break;

    /* Multi-cursor operations */
    case ALT_UP:
      multicursor_add_above();
      break;
    case ALT_DOWN:
      multicursor_add_below();
      break;
    case ALT_C:
      multicursor_add_at_primary();
      break;
    case ALT_V:
      multicursor_add_at_primary_and_advance();
      break;

    case MOUSE_EVENT:
      editor_handle_mouse_event();
      break;

    /* Bracket matching */
    case CTRL_KEY(']'):
      editor_jump_to_matching_bracket();
      break;

    /* Comment toggle operations */
    case 31:  /* Ctrl+/ sends ASCII 31 (0x1F) */
      editor_toggle_line_comment();
      break;
    case CTRL_KEY('\\'):  /* Ctrl+\ for block comment (alternative) */
      editor_toggle_block_comment();
      break;

    case CTRL_KEY('l'):
      break;
    case CHAR_ESCAPE:
      /* Escape: clear secondary cursors and selection */
      if (editor.cursor_count > 0) {
        size_t cleared = editor.cursor_count;
        multicursor_clear();
        editor_set_status_message("Cleared %zu secondary cursor(s)", cleared);
      }
      selection_clear();
      break;

    /* Tab: indent line, Shift+Tab: unindent line */
    case '\t':
      editor_indent_line();
      break;
    case SHIFT_TAB:
      editor_unindent_line();
      break;

    default:
      editor_insert_char(key);
      break;
  }

  quit_times = MITER_QUIT_TIMES;
}

/*** clipboard functions ***/

/* Simple in-memory clipboard */
static char *clipboard_content = NULL;
static int clipboard_content_type = 0;

/* Store content in clipboard.
 * Returns 0 on success, -1 on failure. */
int clipboard_store(const char *content, int content_type) {
  if (!content) return -1;

  free(clipboard_content);
  clipboard_content = strdup(content);
  clipboard_content_type = content_type;

  /* Also sync to system clipboard */
  clipboard_sync_to_system(content);

  return clipboard_content ? 0 : -1;
}

/* Get most recent clipboard entry.
 * Returns malloc'd string, caller must free. Sets content_type if non-NULL. */
char *clipboard_get_latest(int *content_type) {
  if (!clipboard_content) return NULL;

  if (content_type) *content_type = clipboard_content_type;
  return strdup(clipboard_content);
}

/* Sync content TO system clipboard via xsel or xclip.
 * Silently fails if neither tool is available. */
void clipboard_sync_to_system(const char *content) {
  if (!content) return;

  FILE *pipe = popen("xsel --clipboard --input 2>/dev/null", "w");
  if (!pipe) {
    pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
  }
  if (pipe) {
    fwrite(content, 1, strlen(content), pipe);
    pclose(pipe);
  }

  /* Track what we synced for smart merge */
  free(editor.last_system_clipboard);
  editor.last_system_clipboard = strdup(content);
}

/* Read FROM system clipboard via xsel or xclip.
 * Returns malloc'd string, caller must free. Returns NULL if empty/unavailable. */
char *clipboard_read_from_system() {
  char buffer[65536];
  buffer[0] = '\0';

  FILE *pipe = popen("xsel --clipboard --output 2>/dev/null", "r");
  if (!pipe) {
    pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
  }
  if (pipe) {
    size_t len = fread(buffer, 1, sizeof(buffer) - 1, pipe);
    buffer[len] = '\0';
    pclose(pipe);
    return len > 0 ? strdup(buffer) : NULL;
  }
  return NULL;
}

/* Smart merge: check if system clipboard changed externally.
 * If different from last sync, import to internal clipboard before paste. */
void clipboard_smart_merge() {
  char *system_content = clipboard_read_from_system();
  if (!system_content) return;

  /* If different from last sync, update internal clipboard */
  if (!editor.last_system_clipboard ||
      strcmp(system_content, editor.last_system_clipboard) != 0) {
    free(clipboard_content);
    clipboard_content = system_content;
    clipboard_content_type = SELECTION_CHAR;
    free(editor.last_system_clipboard);
    editor.last_system_clipboard = strdup(system_content);
  } else {
    free(system_content);
  }
}

/*** undo/redo system (in-memory) ***/

/* Free an undo entry's allocated strings */
static void undo_entry_free(undo_entry *entry) {
  free(entry->row_content);
  free(entry->char_data);
  free(entry->multi_line);
  entry->row_content = NULL;
  entry->char_data = NULL;
  entry->multi_line = NULL;
}

/* Force start a new undo group for the next operation */
void undo_start_new_group() {
  editor.undo_group_id++;
  editor.undo_position = editor.undo_group_id;
  editor.undo_memory_groups++;
}

/* Check if enough time has passed to start a new undo group */
void undo_maybe_start_group(int force_new) {
  if (force_new) {
    undo_start_new_group();
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long elapsed_ms = (now.tv_sec - editor.last_edit_time.tv_sec) * 1000 +
                    (now.tv_nsec - editor.last_edit_time.tv_nsec) / 1000000;

  if (elapsed_ms > UNDO_GROUP_TIMEOUT_MS || editor.undo_group_id == 0) {
    editor.undo_group_id++;
    editor.undo_position = editor.undo_group_id;
    editor.undo_memory_groups++;
  }

  editor.last_edit_time = now;
}

/* Log an edit operation to the in-memory undo stack */
void undo_log(enum undo_op_type type, int cursor_row, int cursor_col,
              int row_idx, int char_pos, const char *char_data,
              int end_row, int end_col, const char *multi_line) {
  if (editor.undo_logging) return;

  int force_new_group = (type == UNDO_ROW_INSERT || type == UNDO_ROW_DELETE ||
                         type == UNDO_ROW_SPLIT || type == UNDO_SELECTION_DELETE ||
                         type == UNDO_PASTE);

  undo_clear_redo();
  undo_maybe_start_group(force_new_group);

  /* Initialize stack if needed */
  if (!editor.undo_stack) {
    editor.undo_stack_capacity = 256;
    editor.undo_stack = malloc(editor.undo_stack_capacity * sizeof(undo_entry));
    editor.undo_stack_count = 0;
  }

  /* Grow stack if needed */
  if (editor.undo_stack_count >= editor.undo_stack_capacity) {
    if (editor.undo_stack_capacity >= UNDO_MAX_ENTRIES) {
      /* Remove oldest entries to make room */
      int to_remove = editor.undo_stack_capacity / 4;
      for (int i = 0; i < to_remove; i++) {
        undo_entry_free(&editor.undo_stack[i]);
      }
      memmove(editor.undo_stack, &editor.undo_stack[to_remove],
              (editor.undo_stack_count - to_remove) * sizeof(undo_entry));
      editor.undo_stack_count -= to_remove;
    } else {
      editor.undo_stack_capacity *= 2;
      editor.undo_stack = realloc(editor.undo_stack,
                                  editor.undo_stack_capacity * sizeof(undo_entry));
    }
  }

  /* Add new entry */
  undo_entry *entry = &editor.undo_stack[editor.undo_stack_count++];
  entry->group_id = editor.undo_group_id;
  entry->op_type = type;
  entry->cursor_row = cursor_row;
  entry->cursor_col = cursor_col;
  entry->row_idx = row_idx;
  entry->char_pos = char_pos;
  entry->end_row = end_row;
  entry->end_col = end_col;

  /* Copy row content for row operations */
  if ((type == UNDO_ROW_DELETE || type == UNDO_ROW_INSERT) &&
      row_idx >= 0 && row_idx < editor.row_count) {
    entry->row_content = strdup(editor.row[row_idx].chars);
  } else {
    entry->row_content = NULL;
  }

  entry->char_data = char_data ? strdup(char_data) : NULL;
  entry->multi_line = multi_line ? strdup(multi_line) : NULL;

  editor.undo_position = editor.undo_group_id;
}

/* Clear redo history after current position */
void undo_clear_redo() {
  if (editor.undo_position >= editor.undo_group_id) return;
  if (!editor.undo_stack) return;

  /* Remove entries with group_id > undo_position */
  int new_count = editor.undo_stack_count;
  for (int i = editor.undo_stack_count - 1; i >= 0; i--) {
    if (editor.undo_stack[i].group_id > editor.undo_position) {
      undo_entry_free(&editor.undo_stack[i]);
      new_count = i;
    }
  }
  editor.undo_stack_count = new_count;
  editor.undo_group_id = editor.undo_position;
}

/* Perform undo - apply inverse operations for current group */
void editor_undo() {
  if (editor.undo_position <= 0 || !editor.undo_stack) {
    editor_set_status_message("Nothing to undo");
    return;
  }

  editor.undo_logging = 1;
  int target_group = editor.undo_position;
  int ops_undone = 0;
  int restore_row = -1, restore_col = -1;

  /* Process entries in reverse order for this group */
  for (int i = editor.undo_stack_count - 1; i >= 0; i--) {
    undo_entry *e = &editor.undo_stack[i];
    if (e->group_id != target_group) continue;

    if (restore_row == -1) {
      restore_row = e->cursor_row;
      restore_col = e->cursor_col;
    }

    switch (e->op_type) {
      case UNDO_CHAR_INSERT:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count && e->char_pos >= 0) {
          editor_row *row = &editor.row[e->row_idx];
          if (e->char_pos < row->line_size) {
            memmove(&row->chars[e->char_pos], &row->chars[e->char_pos + 1],
                    row->line_size - e->char_pos);
            row->line_size--;
            editor_update_row(row);
            row->dirty = 1;
            editor.dirty++;
          }
        }
        break;

      case UNDO_CHAR_DELETE:
      case UNDO_CHAR_DELETE_FWD:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count && e->char_data && e->char_pos >= 0) {
          editor_row *row = &editor.row[e->row_idx];
          row->chars = realloc(row->chars, row->line_size + 2);
          memmove(&row->chars[e->char_pos + 1], &row->chars[e->char_pos],
                  row->line_size - e->char_pos + 1);
          row->chars[e->char_pos] = e->char_data[0];
          row->line_size++;
          editor_update_row(row);
          row->dirty = 1;
          editor.dirty++;
        }
        break;

      case UNDO_ROW_INSERT:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count) {
          editor_delete_row(e->row_idx);
        }
        break;

      case UNDO_ROW_DELETE:
        if (e->row_content && e->row_idx >= 0) {
          editor_insert_row(e->row_idx, e->row_content, strlen(e->row_content));
        }
        break;

      case UNDO_ROW_SPLIT:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count - 1) {
          editor_row *row = &editor.row[e->row_idx];
          editor_row *next = &editor.row[e->row_idx + 1];
          row->chars = realloc(row->chars, row->line_size + next->line_size + 1);
          memcpy(&row->chars[row->line_size], next->chars, next->line_size);
          row->line_size += next->line_size;
          row->chars[row->line_size] = '\0';
          editor_update_row(row);
          row->dirty = 1;
          editor_delete_row(e->row_idx + 1);
        }
        break;

      case UNDO_SELECTION_DELETE:
        if (e->multi_line) {
          editor.cursor_y = e->cursor_row;
          editor.cursor_x = e->cursor_col;
          for (const char *p = e->multi_line; *p; p++) {
            if (*p == '\n') editor_insert_newline();
            else editor_insert_char(*p);
          }
        }
        break;

      case UNDO_PASTE:
        if (e->multi_line) {
          editor.selection.active = 1;
          editor.selection.anchor.row = e->cursor_row;
          editor.selection.anchor.col = e->cursor_col;
          editor.selection.cursor.row = e->end_row;
          editor.selection.cursor.col = e->end_col;
          selection_delete();
        }
        break;
    }
    ops_undone++;
  }

  if (restore_row >= 0) {
    editor.cursor_y = restore_row;
    editor.cursor_x = restore_col;
    if (editor.cursor_y >= editor.row_count)
      editor.cursor_y = editor.row_count > 0 ? editor.row_count - 1 : 0;
    if (editor.cursor_y < editor.row_count &&
        editor.cursor_x > editor.row[editor.cursor_y].line_size)
      editor.cursor_x = editor.row[editor.cursor_y].line_size;
  }

  editor.undo_position--;
  editor.undo_logging = 0;
  editor_set_status_message("Undo (%d operation%s)", ops_undone, ops_undone == 1 ? "" : "s");
}

/* Perform redo - re-apply operations for next group */
void editor_redo() {
  if (editor.undo_position >= editor.undo_group_id || !editor.undo_stack) {
    editor_set_status_message("Nothing to redo");
    return;
  }

  editor.undo_position++;
  editor.undo_logging = 1;
  int target_group = editor.undo_position;
  int ops_redone = 0;
  int last_row = -1, last_col = -1;

  /* Process entries in forward order for this group */
  for (int i = 0; i < editor.undo_stack_count; i++) {
    undo_entry *e = &editor.undo_stack[i];
    if (e->group_id != target_group) continue;

    last_row = e->cursor_row;
    last_col = e->cursor_col;

    switch (e->op_type) {
      case UNDO_CHAR_INSERT:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count && e->char_data && e->char_pos >= 0) {
          editor_row *row = &editor.row[e->row_idx];
          row->chars = realloc(row->chars, row->line_size + 2);
          memmove(&row->chars[e->char_pos + 1], &row->chars[e->char_pos],
                  row->line_size - e->char_pos + 1);
          row->chars[e->char_pos] = e->char_data[0];
          row->line_size++;
          editor_update_row(row);
          row->dirty = 1;
          editor.dirty++;
          last_col = e->char_pos + 1;
        }
        break;

      case UNDO_CHAR_DELETE:
      case UNDO_CHAR_DELETE_FWD:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count && e->char_pos >= 0) {
          editor_row *row = &editor.row[e->row_idx];
          if (e->char_pos < row->line_size) {
            memmove(&row->chars[e->char_pos], &row->chars[e->char_pos + 1],
                    row->line_size - e->char_pos);
            row->line_size--;
            editor_update_row(row);
            row->dirty = 1;
            editor.dirty++;
          }
        }
        break;

      case UNDO_ROW_INSERT:
        if (e->row_content && e->row_idx >= 0) {
          editor_insert_row(e->row_idx, e->row_content, strlen(e->row_content));
        }
        break;

      case UNDO_ROW_DELETE:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count) {
          editor_delete_row(e->row_idx);
        }
        break;

      case UNDO_ROW_SPLIT:
        if (e->row_idx >= 0 && e->row_idx < editor.row_count && e->char_pos >= 0) {
          editor.cursor_y = e->row_idx;
          editor.cursor_x = e->char_pos;
          editor_insert_newline();
        }
        break;

      case UNDO_SELECTION_DELETE:
        if (e->multi_line) {
          editor.selection.active = 1;
          editor.selection.anchor.row = e->cursor_row;
          editor.selection.anchor.col = e->cursor_col;
          editor.selection.cursor.row = e->end_row;
          editor.selection.cursor.col = e->end_col;
          selection_delete();
        }
        break;

      case UNDO_PASTE:
        if (e->multi_line) {
          editor.cursor_y = e->cursor_row;
          editor.cursor_x = e->cursor_col;
          for (const char *p = e->multi_line; *p; p++) {
            if (*p == '\n') editor_insert_newline();
            else editor_insert_char(*p);
          }
          last_row = editor.cursor_y;
          last_col = editor.cursor_x;
        }
        break;
    }
    ops_redone++;
  }

  if (last_row >= 0) {
    editor.cursor_y = last_row;
    editor.cursor_x = last_col;
    if (editor.cursor_y >= editor.row_count)
      editor.cursor_y = editor.row_count > 0 ? editor.row_count - 1 : 0;
    if (editor.cursor_y < editor.row_count &&
        editor.cursor_x > editor.row[editor.cursor_y].line_size)
      editor.cursor_x = editor.row[editor.cursor_y].line_size;
  }

  editor.undo_logging = 0;
  editor_set_status_message("Redo (%d operation%s)", ops_redone, ops_redone == 1 ? "" : "s");
}

/* Simple strstr-based search (replaces FTS5) */
void simple_search(const char *query) {
  /* Clear previous results */
  if (editor.search_results) {
    free(editor.search_results);
    editor.search_results = NULL;
  }
  editor.search_result_count = 0;

  if (!query || strlen(query) == 0) return;

  /* Allocate initial results array */
  editor.search_result_capacity = INITIAL_SEARCH_RESULT_CAPACITY;
  editor.search_results = malloc(editor.search_result_capacity * sizeof(search_result));

  /* Search each line */
  for (int line_num = 0; line_num < editor.row_count; line_num++) {
    char *line_start = editor.row[line_num].render;
    char *search_pos = line_start;
    char *match_pos;

    while ((match_pos = strstr(search_pos, query)) != NULL) {
      /* Expand results array if needed */
      if (editor.search_result_count >= editor.search_result_capacity) {
        editor.search_result_capacity *= 2;
        editor.search_results = realloc(editor.search_results,
                                    editor.search_result_capacity * sizeof(search_result));
      }

      editor.search_results[editor.search_result_count].line_number = line_num;
      editor.search_results[editor.search_result_count].match_offset = match_pos - line_start;
      editor.search_results[editor.search_result_count].match_length = strlen(query);
      editor.search_result_count++;

      search_pos = match_pos + 1;
    }
  }
}

/*** theming ***/

/* Compare two RGB colors for equality. */
int rgb_equal(rgb_color color_a, rgb_color color_b) {
  return color_a.r == color_b.r && color_a.g == color_b.g && color_a.b == color_b.b;
}

/* Get a color from the active theme by color ID.
 * Returns white as fallback for invalid IDs. */
rgb_color theme_get_color(enum theme_color color_id) {
  if (color_id >= 0 && color_id < THEME_COLOR_COUNT) {
    return active_theme[color_id];
  }
  /* Fallback to white if invalid color_id */
  rgb_color fallback = {255, 255, 255};
  return fallback;
}

/* Write ANSI escape sequence to set foreground text color. */
void set_foreground_rgb(struct append_buffer *ab, rgb_color color) {
  char color_buffer[COLOR_ESCAPE_BUFFER_SIZE];
  int length = snprintf(color_buffer, sizeof(color_buffer), ESCAPE_FOREGROUND_RGB_FORMAT,
                     color.r, color.g, color.b);
  append_buffer_write(ab, color_buffer, length);
}

/* Write ANSI escape sequence to set background color. */
void set_background_rgb(struct append_buffer *ab, rgb_color color) {
  char color_buffer[COLOR_ESCAPE_BUFFER_SIZE];
  int length = snprintf(color_buffer, sizeof(color_buffer), ESCAPE_BACKGROUND_RGB_FORMAT,
                     color.r, color.g, color.b);
  append_buffer_write(ab, color_buffer, length);
}

/* Write ANSI escape sequence to reset all text attributes. */
void reset_colors(struct append_buffer *ab) {
  append_buffer_write(ab, ESCAPE_RESET_ATTRIBUTES, ESCAPE_RESET_ATTRIBUTES_LEN);
}

/* Number of color slots in a theme (compile-time constant) */
#define THEME_COLOR_SLOT_COUNT (sizeof(theme_color_names) / sizeof(theme_color_names[0]))

/* Map color name string to theme_color enum index.
 * Returns -1 if name not found. */
int theme_color_name_to_index(const char *name) {
  for (int i = 0; i < (int)THEME_COLOR_SLOT_COUNT; i++) {
    if (strcmp(theme_color_names[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

/* Add a theme to the runtime registry. */
void theme_registry_add(const char *name, rgb_color *colors) {
  /* Grow capacity if needed */
  if (loaded_theme_count >= loaded_theme_capacity) {
    int new_capacity = loaded_theme_capacity == 0 ? 8 : loaded_theme_capacity * 2;
    runtime_theme *new_themes = realloc(loaded_themes, new_capacity * sizeof(runtime_theme));
    if (!new_themes) return;  /* Out of memory, skip this theme */
    loaded_themes = new_themes;
    loaded_theme_capacity = new_capacity;
  }

  /* Add the theme */
  runtime_theme *theme = &loaded_themes[loaded_theme_count];
  theme->name = strdup(name);
  if (!theme->name) return;  /* Out of memory */
  memcpy(theme->colors, colors, sizeof(theme->colors));
  loaded_theme_count++;
}

/* Free all loaded themes. */
void theme_registry_free() {
  for (int i = 0; i < loaded_theme_count; i++) {
    free(loaded_themes[i].name);
  }
  free(loaded_themes);
  loaded_themes = NULL;
  loaded_theme_count = 0;
  loaded_theme_capacity = 0;
}

/* Parse a .def file and add theme to registry.
 * Returns 1 on success, 0 on failure. */
int theme_load_from_file(const char *filepath) {
  FILE *f = fopen(filepath, "r");
  if (!f) return 0;

  char name[64] = "Unknown";
  rgb_color colors[THEME_COLOR_COUNT];
  /* Initialize with fallback colors */
  memcpy(colors, fallback_theme_colors, sizeof(colors));

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    /* Parse @name from comment header */
    char name_buf[64];
    if (sscanf(line, "/* @name: %63[^*]", name_buf) == 1) {
      /* Trim trailing spaces and copy */
      char *end = name_buf + strlen(name_buf) - 1;
      while (end > name_buf && (*end == ' ' || *end == '\t')) end--;
      *(end + 1) = '\0';
      strncpy(name, name_buf, sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';
      continue;
    }

    /* Parse X(NAME, r, g, b, desc) */
    char color_name[64];
    int r, g, b;
    if (sscanf(line, "X(%63[^,], %d, %d, %d,", color_name, &r, &g, &b) == 4) {
      int idx = theme_color_name_to_index(color_name);
      if (idx >= 0 && idx < (int)THEME_COLOR_SLOT_COUNT) {
        colors[idx].r = (unsigned char)r;
        colors[idx].g = (unsigned char)g;
        colors[idx].b = (unsigned char)b;
      }
    }
  }
  fclose(f);

  /* Add to registry */
  theme_registry_add(name, colors);
  return 1;
}

/* Load all .def files from a directory. */
void theme_load_directory(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) return;

  struct dirent *entry;
  while ((entry = readdir(d))) {
    /* Only process .def files */
    size_t name_len = strlen(entry->d_name);
    if (name_len < 4) continue;
    if (strcmp(entry->d_name + name_len - 4, ".def") != 0) continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    theme_load_from_file(path);
  }
  closedir(d);
}

/* Discover and load themes from bundled and user directories. */
void theme_discover_all() {
  /* Load from bundled themes first (./themes/) */
  theme_load_directory("./themes");

  /* Then user themes (~/.config/terra/themes/) - can override */
  char *home = getenv("HOME");
  if (home) {
    char user_path[PATH_MAX];
    snprintf(user_path, sizeof(user_path), "%s/.config/terra/themes", home);
    theme_load_directory(user_path);
  }

  /* If no themes were loaded, create a fallback */
  if (loaded_theme_count == 0) {
    theme_registry_add("Fallback", (rgb_color *)fallback_theme_colors);
  }
}

/* Find theme index by name. Returns -1 if not found. */
int theme_find_by_name(const char *name) {
  for (int i = 0; i < loaded_theme_count; i++) {
    if (strcmp(loaded_themes[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/* Initialize theming system: discover themes and load saved preference. */
void theme_init() {
  /* Discover and load all themes from disk */
  theme_discover_all();

  /* Try to load saved theme by name */
  char saved_name[64];
  if (theme_load_name_from_config(saved_name, sizeof(saved_name))) {
    int idx = theme_find_by_name(saved_name);
    if (idx >= 0) {
      editor.current_theme_index = idx;
      theme_load(idx);
      return;
    }
  }

  /* Fallback to first theme */
  editor.current_theme_index = 0;
  theme_load(0);
}

/* Load a theme by index into the active color palette. */
void theme_load(int index) {
  if (loaded_theme_count == 0) {
    /* No themes loaded, use fallback colors */
    memcpy(active_theme, fallback_theme_colors, sizeof(active_theme));
    return;
  }
  if (index < 0 || index >= loaded_theme_count) {
    index = 0;
  }
  editor.current_theme_index = index;
  memcpy(active_theme, loaded_themes[index].colors, sizeof(active_theme));
}

/* Cycle to next theme and save preference. */
void theme_cycle() {
  if (loaded_theme_count == 0) return;
  int next_index = (editor.current_theme_index + 1) % loaded_theme_count;
  theme_load(next_index);
  theme_save();
  editor_set_status_message("Theme: %s", loaded_themes[next_index].name);
}

/* Get the display name of the current theme. */
const char* theme_get_name() {
  if (loaded_theme_count == 0) return "Fallback";
  if (editor.current_theme_index < 0 || editor.current_theme_index >= loaded_theme_count) {
    return "Unknown";
  }
  return loaded_themes[editor.current_theme_index].name;
}

/* Save current theme (by name) and preferences to config file. */
void theme_save() {
  FILE *file_pointer = fopen("terra.conf", "w");
  if (file_pointer == NULL) return;

  /* Save theme by name instead of index for stability */
  const char *theme_name = theme_get_name();
  fprintf(file_pointer, "theme=%s\n", theme_name);
  fprintf(file_pointer, "show_line_numbers=%d\n", editor.show_line_numbers);
  fclose(file_pointer);
}

/* Load theme name from config file into name_buf.
 * Returns 1 if found, 0 if not found or error. */
int theme_load_name_from_config(char *name_buf, int buf_size) {
  FILE *file_pointer = fopen("terra.conf", "r");
  if (file_pointer == NULL) {
    editor.show_line_numbers = 1;
    return 0;
  }

  char line[CONFIG_LINE_BUFFER_SIZE];
  int found_theme = 0;
  int line_numbers = 1;

  while (fgets(line, sizeof(line), file_pointer)) {
    /* Try to parse theme=Name */
    if (strncmp(line, "theme=", 6) == 0) {
      char *value = line + 6;
      /* Remove trailing newline */
      char *nl = strchr(value, '\n');
      if (nl) *nl = '\0';
      /* Copy as theme name (old numeric format will fail lookup, using fallback) */
      strncpy(name_buf, value, buf_size - 1);
      name_buf[buf_size - 1] = '\0';
      found_theme = 1;
    } else if (sscanf(line, "show_line_numbers=%d", &line_numbers) == 1) {
      editor.show_line_numbers = line_numbers;
    }
  }

  fclose(file_pointer);
  return found_theme;
}

/* Recalculate gutter width based on line count and settings. */
void editor_update_gutter_width() {
  if (!editor.show_line_numbers) {
    editor.gutter_width = 0;
    return;
  }

  /* Calculate number of digits needed for line numbers */
  int digits = snprintf(NULL, 0, "%d", editor.row_count);
  if (digits < 1) digits = 1;

  /* Gutter width = digits + 1 trailing space */
  editor.gutter_width = digits + 1;
}

/* Toggle line number display and save preference. */
void editor_toggle_line_numbers() {
  editor.show_line_numbers = !editor.show_line_numbers;
  editor_update_gutter_width();
  /* Save preference to config */
  theme_save();
  editor_set_status_message("Line numbers %s", editor.show_line_numbers ? "ON" : "OFF");
}

/* Toggle soft wrap mode for visual line wrapping. */
void editor_toggle_soft_wrap() {
  editor.soft_wrap = !editor.soft_wrap;
  editor_set_status_message("Soft wrap %s", editor.soft_wrap ? "ON" : "OFF");
}

/* Toggle center/typewriter scrolling mode. */
void editor_toggle_center_scroll() {
  editor.center_scroll = !editor.center_scroll;
  editor_set_status_message("Center scroll %s", editor.center_scroll ? "ON" : "OFF");
}

/*** init ***/

/* Initialize all editor state to defaults. Must be called before use. */
void editor_init() {
  editor.cursor_x = 0;
  editor.cursor_y = 0;
  editor.render_x = 0;
  editor.row_offset = 0;
  editor.column_offset = 0;
  editor.row_count = 0;
  editor.row = NULL;
  editor.dirty = 0;
  editor.filename = NULL;
  editor.status_message[0] = '\0';
  editor.status_message_time = 0;
  editor.syntax = NULL;
  editor.search_results = NULL;
  editor.search_result_count = 0;
  editor.search_result_capacity = 0;
  editor.wrap_column = DEFAULT_WRAP_COLUMN;
  /* Soft wrap disabled by default */
  editor.soft_wrap = 0;
  /* Center/typewriter scrolling enabled by default */
  editor.center_scroll = 1;
  /* Tactile scroll: start at speed 1, initialize timestamp */
  editor.scroll_speed = 1;
  clock_gettime(CLOCK_MONOTONIC, &editor.last_scroll_time);
  editor.cursors_follow_primary = 1;
  editor.allow_primary_overlap = 0;
  /* Initialize undo system */
  editor.undo_stack = NULL;
  editor.undo_stack_count = 0;
  editor.undo_stack_capacity = 0;
  clock_gettime(CLOCK_MONOTONIC, &editor.last_edit_time);

  /* Menu bar visible by default */
  editor.menu_bar_visible = 1;
  editor.menu_open = -1;
  editor.menu_selected_item = 0;

  if (window_get_size(&editor.screen_rows, &editor.screen_columns) == -1) die("window_get_size");

  /* Enforce minimum usable dimensions */
  if (editor.screen_columns < 10) editor.screen_columns = 10;
  if (editor.screen_rows < 3) editor.screen_rows = 3;

  /* Reserve rows for UI: status bar + message bar + menu bar (if visible) */
  int reserved = SCREEN_RESERVED_ROWS + (editor.menu_bar_visible ? 1 : 0);
  editor.screen_rows -= reserved;
  if (editor.screen_rows < 1) editor.screen_rows = 1;

  /* Register signal handler for terminal resize */
  signal(SIGWINCH, handle_sigwinch);

  theme_init();
  editor_update_gutter_width();
}


/* Entry point: Miter text editor */
int main(int argc, char *argv[]) {
  enable_raw_mode();
  editor_init();

  if (argc >= 2) {
    editor_open(argv[1]);
  }

  editor_set_status_message(
    "Miter | Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  while (1) {
    /* Handle pending terminal resize */
    if (window_resize_pending) {
      window_resize_pending = 0;
      editor_handle_resize();
    }
    editor_refresh_screen();
    editor_process_keypress();
  }

  return 0;
}
