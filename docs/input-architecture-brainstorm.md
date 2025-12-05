# Brainstorm: Advanced Input Handling Architecture

## Design Constraints

1. **Kitty keyboard protocol only** - Drop legacy terminal support
2. **Support complex input methods**:
   - Key chords (multiple keys simultaneously)
   - Quasimodes / hold-to-act layers (Raskin's philosophy)
   - Sticky modifiers
   - Dual-role keys (tap vs hold)
3. **User-configurable keybindings** - Essential feature
4. **Minimize mode errors** - Kinesthetic feedback, user-maintained modes

## Kitty Protocol Advantages

The Kitty keyboard protocol provides features essential for advanced input:
- **Key press/release events** - Can detect when keys are held vs tapped
- **Modifier key events** - Reports when Shift/Ctrl/Alt are pressed/released alone
- **Unambiguous encoding** - Every key has a unique code
- **Event types** - Press (1), Repeat (2), Release (3)

This enables quasimodes, chords, and dual-role keys that legacy terminals cannot support.

---

## Approach 1: Quasimodal Command Layers (Hold-to-Act)

**Concept**: Temporary modes that exist only while a trigger key is held. Release returns to insert mode. This is Jef Raskin's "quasimode" philosophy (e.g., Shift vs CapsLock).

**Why it matters**:
- Always-safe typing state (no mode errors)
- Dense command sets without latching global modes
- Kinesthetic feedback (held key) beats visual-only feedback
- User-maintained modes produce fewer errors than system-latched modes

**Implementation**:

```c
typedef enum {
    LAYER_INSERT,       // Normal typing
    LAYER_COMMAND,      // Held Space: h/j/k/l navigate, d deletes, etc.
    LAYER_SELECT,       // Held Shift: movement extends selection
    LAYER_WINDOW,       // Held Ctrl+W: window management
    // User-defined layers...
} input_layer;

typedef struct {
    int trigger_key;        // Key that activates this layer (e.g., Space)
    input_layer layer;      // Which layer to activate
    int tap_action;         // Action if tapped (e.g., insert space)
    int hold_threshold_ms;  // Time to distinguish tap vs hold (150-200ms)
    int grace_period_ms;    // Time after release to finish chord (300-500ms)
} layer_trigger;

typedef struct {
    input_layer layer;
    int key_code;
    void (*handler)(void);
    const char *description;
} layer_binding;

static layer_trigger triggers[] = {
    { KEY_SPACE, LAYER_COMMAND, ACTION_INSERT_SPACE, 200, 400 },
    { KEY_CAPSLOCK, LAYER_COMMAND, ACTION_NONE, 0, 400 },  // Hold only, no tap
};

static layer_binding command_layer[] = {
    { LAYER_COMMAND, 'h', cmd_move_left, "Move left" },
    { LAYER_COMMAND, 'j', cmd_move_down, "Move down" },
    { LAYER_COMMAND, 'k', cmd_move_up, "Move up" },
    { LAYER_COMMAND, 'l', cmd_move_right, "Move right" },
    { LAYER_COMMAND, 'd', cmd_delete_line, "Delete line" },
    { LAYER_COMMAND, 'w', cmd_next_word, "Next word" },
    { LAYER_COMMAND, 'b', cmd_prev_word, "Previous word" },
    // ...
};
```

**State Machine**:

```
                    ┌──────────────────────────────────────┐
                    │           LAYER_INSERT               │
                    │  (normal typing, all keys insert)    │
                    └──────────────┬───────────────────────┘
                                   │
                    Space pressed  │
                    (start timer)  │
                                   ▼
                    ┌──────────────────────────────────────┐
                    │         PENDING_LAYER                │
                    │  (waiting to see if tap or hold)     │
                    └──────┬───────────────────┬───────────┘
                           │                   │
        Released < 200ms   │                   │  Held > 200ms OR
        (it was a tap)     │                   │  another key pressed
                           ▼                   ▼
                    ┌──────────────┐   ┌───────────────────┐
                    │ Insert space │   │   LAYER_COMMAND   │
                    │ Return to    │   │ h/j/k/l navigate  │
                    │ INSERT       │   │ d deletes, etc.   │
                    └──────────────┘   └─────────┬─────────┘
                                                 │
                                   Space released│
                                   (+ grace 400ms)
                                                 │
                                                 ▼
                                       Return to INSERT
```

**Feedback Requirements**:
- Status line indicator: `[CMD]`, `[SEL]`, etc.
- Cursor shape change (block → underline)
- Brief HUD overlay for layer activation

**Pros**:
- Eliminates mode errors (can't forget which mode you're in)
- Familiar to anyone who uses Shift, Ctrl, Alt
- Dense command vocabulary without memorizing mode switches
- Smooth transition: if it feels wrong, just release

**Cons**:
- Requires Kitty protocol for press/release detection
- Timing-sensitive (need tunable thresholds)
- Learning curve for "hold Space" paradigm

---

## Approach 2: Key Chords (Simultaneous Keys)

**Concept**: Multiple keys pressed together (not sequenced) trigger actions. Different from Ctrl+X which is modifier+key.

**Examples**:
- `j + k` simultaneously = Escape (like some Vim configs)
- `f + d` = delete word
- `s + d` = save

**Implementation**:

```c
typedef struct {
    int keys[4];        // Up to 4 simultaneous keys
    int key_count;
    int max_delay_ms;   // Max time between first and last key (50ms typical)
    void (*handler)(void);
} chord_binding;

typedef struct {
    int keys_held[16];  // Currently held keys
    int held_count;
    uint64_t first_press_time;
    int pending_chord;  // Waiting to see if more keys join
} chord_state;

static chord_binding chords[] = {
    { {'j', 'k'}, 2, 50, cmd_escape },
    { {'f', 'd'}, 2, 50, cmd_delete_word },
    // ...
};

void process_key_event(int key, int event_type) {
    if (event_type == KEY_PRESS) {
        add_to_held(key);
        check_for_chord_match();
    } else if (event_type == KEY_RELEASE) {
        remove_from_held(key);
    }
}
```

**Timing Diagram**:
```
Time:     0ms    20ms   40ms   60ms   80ms   100ms
          │      │      │      │      │      │
Key 'j':  ▼──────────────────────────────────▲ (press → release)
Key 'k':         ▼─────────────────────▲       (press → release)
                 │                     │
                 └─ Both held within 50ms = CHORD DETECTED
```

**Pros**:
- Very fast for experts (faster than sequences)
- Natural feeling (like piano chords)
- Can reduce modifier fatigue

**Cons**:
- Hard to type "jk" normally (need rollover detection)
- Keyboard hardware must support N-key rollover
- Timing-sensitive

---

## Approach 3: Leader Key Sequences (Vim/Emacs Style)

**Concept**: A prefix key (leader) followed by a sequence of keys. Times out if sequence not completed.

**Examples**:
- `<leader>w` = save (leader is often Space or ,)
- `<leader>ff` = find file
- `<leader>gs` = git status

**Implementation**:

```c
typedef struct sequence_node {
    int key;
    void (*handler)(void);           // NULL if not terminal
    struct sequence_node *children;  // Next level of sequence
    int child_count;
    const char *description;
} sequence_node;

typedef struct {
    sequence_node *current;
    uint64_t start_time;
    int timeout_ms;  // 1000ms typical
    char display[32];  // For status: "SPC f ..."
} sequence_state;

// Tree structure: SPC → f → f = find_file
//                     → s = save
//                     → g → s = git_status
//                         → c = git_commit
```

**Pros**:
- Unlimited command space (can nest arbitrarily deep)
- Self-documenting with which-key style popups
- Familiar to Vim/Emacs users
- No timing precision required (just timeout)

**Cons**:
- Slower than chords or direct bindings
- Requires memorization of sequences
- Discoverability problem without helper UI

---

## Approach 4: Dual-Role Keys

**Concept**: A key does different things when tapped vs held.

**Examples**:
- Space: tap = insert space, hold = command layer
- Caps Lock: tap = escape, hold = ctrl
- Enter: tap = newline, hold = shift

**Implementation**:

```c
typedef struct {
    int key;
    int tap_action;       // What to do on tap
    int hold_action;      // What to do on hold (or which layer to activate)
    int hold_threshold_ms;
    int is_layer_trigger; // Does hold activate a layer?
} dual_role_key;

static dual_role_key dual_roles[] = {
    { KEY_SPACE, ACTION_INSERT_SPACE, LAYER_COMMAND, 200, 1 },
    { KEY_CAPSLOCK, ACTION_ESCAPE, MOD_CTRL, 150, 0 },
    { KEY_ENTER, ACTION_NEWLINE, MOD_SHIFT, 200, 0 },
};
```

**Pros**:
- Reduces hand movement (home row mods)
- Familiar concept (already used in QMK/ZMK keyboard firmware)
- Combines well with layers

**Cons**:
- Timing sensitivity (frustrating if thresholds wrong)
- Can't hold for repeat (e.g., hold Space to insert many spaces)
- Learning curve

---

## Approach 5: Sticky Modifiers

**Concept**: Tap a modifier to "stick" it for the next keypress. Useful for accessibility and reducing strain.

**Examples**:
- Tap Ctrl → next key is Ctrl+key
- Double-tap Ctrl → lock Ctrl until tapped again

**Implementation**:

```c
typedef enum {
    MOD_STATE_NONE,
    MOD_STATE_STICKY,   // One-shot: applies to next key only
    MOD_STATE_LOCKED,   // Locked until toggled off
} modifier_state;

typedef struct {
    modifier_state ctrl;
    modifier_state alt;
    modifier_state shift;
    uint64_t last_tap_time[3];  // For double-tap detection
} sticky_modifiers;
```

**Pros**:
- Accessibility benefit (one-handed operation)
- Reduces strain from holding modifiers
- Standard feature in many OSes

**Cons**:
- Can cause confusion if forgotten
- Need clear visual feedback

---

## Approach 6: Unified State Machine Architecture

**Concept**: A layered state machine that handles all input processing from raw bytes to action dispatch.

```c
/*
 * Input Event - the unified representation of all input
 */
typedef struct {
    int key_code;           // Unicode codepoint or special key
    int modifiers;          // SHIFT | CTRL | ALT | SUPER
    int event_type;         // PRESS, REPEAT, RELEASE
    uint64_t timestamp_ms;
} input_event;

/*
 * Input State - tracks all temporal state for complex input
 */
typedef struct {
    // Current layer
    input_layer active_layer;
    input_layer pending_layer;
    uint64_t layer_trigger_time;

    // Keys currently held (for chords)
    int held_keys[16];
    int held_count;

    // Sequence state (leader key)
    sequence_node *sequence_position;
    uint64_t sequence_start_time;

    // Sticky modifiers
    sticky_modifiers sticky;

    // Dual-role key pending
    int pending_dual_key;
    uint64_t dual_key_press_time;
} input_state;

/*
 * Main input processing function
 */
int process_input_event(input_state *state, input_event *event) {
    // 1. Update held keys
    update_held_keys(state, event);

    // 2. Check for chord completion
    int chord_action = check_chords(state);
    if (chord_action) return chord_action;

    // 3. Handle dual-role key resolution
    int dual_action = resolve_dual_role(state, event);
    if (dual_action) return dual_action;

    // 4. Check layer triggers
    update_layer_state(state, event);

    // 5. Apply sticky modifiers
    apply_sticky_modifiers(state, event);

    // 6. Lookup binding for current layer
    return lookup_binding(state->active_layer, event);
}
```

**Key insight**: All the approaches (quasimodes, chords, sequences, dual-role, sticky) can coexist in a unified state machine. They operate at different phases of input processing.

---

## Approach 7: Kitty Protocol Native Parsing (Kitty-Only)

**Concept**: Since we're dropping legacy support, simplify to Kitty protocol only with full event type support.

**Kitty Protocol Features We Need**:
- CSI u format: `CSI keycode [; modifiers [: event_type]] u`
- Event types: 1 = press, 2 = repeat, 3 = release
- Modifier reporting: reports Shift/Ctrl/Alt/Super separately
- Full key release detection (essential for quasimodes)

```c
typedef struct {
    int key_code;       // Unicode codepoint or functional key
    int modifiers;      // Bitmask: SHIFT=1, ALT=2, CTRL=4, SUPER=8
    int event_type;     // 1=press, 2=repeat, 3=release
    uint64_t timestamp; // For timing-based detection
} kitty_key_event;

kitty_key_event parse_kitty_input(void) {
    // Read CSI sequence
    // Parse: keycode ; modifiers : event_type u
    // Extract all fields
    // Add timestamp
}
```

**Pros**:
- Simplified codebase (remove ~200 lines of legacy handling)
- Full access to press/repeat/release events
- Unambiguous key identification
- Modifier keys reportable as events themselves

**Cons**:
- Requires Kitty, WezTerm, foot, or other modern terminal
- Users with older terminals cannot use miter

---

## Recommended Architecture

### Layer 1: Kitty Protocol Parser

```c
kitty_key_event read_key_event(void);  // Raw Kitty parsing
```

### Layer 2: Input State Machine

```c
typedef struct {
    // Layer state (quasimodes)
    input_layer current_layer;
    int layer_trigger_held;      // Which key activated current layer
    uint64_t layer_enter_time;

    // Held keys (for chords)
    int held_keys[16];
    int held_count;
    uint64_t chord_window_start;

    // Dual-role detection
    int pending_tap_key;
    uint64_t tap_press_time;
    int tap_interrupted;         // Another key pressed while held

    // Sequence state (leader key)
    sequence_node *seq_position;
    uint64_t seq_start_time;

    // Sticky modifiers
    int sticky_mods;             // Bitmask of sticky modifiers
    int locked_mods;             // Bitmask of locked modifiers
} input_state;
```

### Layer 3: Binding Resolution

```c
typedef struct {
    int layer;              // Which layer this binding is in
    int key_code;
    int required_mods;      // Must have these modifiers
    int forbidden_mods;     // Must NOT have these modifiers
    const char *action;     // Action name for config
    void (*handler)(void);
} binding;

// Check bindings in order: user → layer-specific → default
action_result resolve_binding(input_state *state, kitty_key_event *event);
```

### Layer 4: Action Dispatch

```c
// Named actions that can be bound to keys
typedef struct {
    const char *name;
    void (*handler)(void);
    const char *description;
} action_def;

static action_def actions[] = {
    { "move_up", cmd_move_up, "Move cursor up" },
    { "move_down", cmd_move_down, "Move cursor down" },
    { "save", cmd_save, "Save file" },
    { "quit", cmd_quit, "Quit editor" },
    // ... all actions
};
```

---

## Complete Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    Terminal (stdin)                              │
│            CSI keycode ; modifiers : event_type u               │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              Layer 1: Kitty Protocol Parser                      │
│  read_key_event() → kitty_key_event                             │
│  { key_code, modifiers, event_type, timestamp }                 │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              Layer 2: Input State Machine                        │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Chord        │  │ Dual-Role    │  │ Layer        │          │
│  │ Detection    │  │ Resolution   │  │ Management   │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  process_input_event(state, event)                              │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              Layer 3: Binding Resolution                         │
│                                                                  │
│  1. Check user bindings for current layer                       │
│  2. Check default bindings for current layer                    │
│  3. Check fallback layer (INSERT)                               │
│                                                                  │
│  resolve_binding(state, event) → action_name                    │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│              Layer 4: Action Dispatch                            │
│                                                                  │
│  lookup_action(action_name) → handler()                         │
│  Execute handler                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Config File Design

```ini
[input]
# Timing settings (milliseconds)
tap_threshold = 200        # Max time for tap vs hold distinction
chord_window = 50          # Max time between keys in a chord
sequence_timeout = 1000    # Timeout for leader key sequences
layer_grace_period = 400   # Time after release to finish commands

[layers]
# Define custom layers
command = Space            # Hold Space for command layer
window = Ctrl+W            # Hold Ctrl+W for window layer

[layer.command]
# Bindings when command layer is active (holding Space)
h = move_left
j = move_down
k = move_up
l = move_right
w = next_word
b = prev_word
d = delete_line
u = undo
/ = find

[layer.insert]
# Normal mode bindings
Ctrl+S = save
Ctrl+Q = quit
Ctrl+Z = undo
Ctrl+F = find
Alt+T = cycle_theme

[chords]
# Simultaneous key chords
j+k = escape              # Press j and k together
f+d = delete_word

[dual_role]
# Tap vs hold behavior
Space = insert_space | layer:command    # Tap: space, Hold: command layer
CapsLock = escape | modifier:ctrl       # Tap: escape, Hold: ctrl

[sticky]
# Modifier sticky behavior
# Options: off, oneshot, lock_on_double
Ctrl = oneshot
Alt = oneshot
Shift = off
```

---

## Editor Philosophy References

**Raskin's Quasimodes**: User-maintained modes (held keys) vs system-latched modes. Canon Cat philosophy.

**Kakoune**: Selection-first, interactive grammar (select then act). Worth considering for selection model.

**Helix**: Tree-sitter structural selection. AST-aware navigation/selection.

**Emacs**: Prefix keys, transient maps, discoverability via M-x. Decades of evidence that modeless + chords scales.

**Sam/Acme**: Structural regex command language, mouse-centric "text as UI".

---

## Notes

- This is a **brainstorming document** for future reference
- No implementation is planned at this time
- **Key decisions**:
  1. Kitty protocol only (drop legacy)
  2. Quasimodes as primary advanced input method
  3. User-configurable everything via config file
  4. Layered architecture for clean separation
