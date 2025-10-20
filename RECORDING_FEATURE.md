# ppstep Recording Feature Documentation

## Overview
The recording feature has been added to ppstep to allow capturing the complete macro expansion process to a text file for offline analysis.

## New Commands

### `record <filename>` or `rec <filename>`
Starts recording all preprocessing events to the specified file.

Example:
```
pp> record output.txt
Recording to output.txt
```

or

```
pp> rec trace.txt
Recording to trace.txt
```

### `stoprecord` or `sr`
Stops the current recording session.

Example:
```
pp> stoprecord
Recording stopped
```

or

```
pp> sr
Recording stopped
```

### `status`
Shows the current recording status.

Example:
```
pp> status
Recording to: output.txt
```

or when not recording:
```
pp> status
Not recording
```

## Usage Workflows

### 1. Recording All Steps
```bash
# Start ppstep
$ ./ppstep test_macros.c

# Start recording
pp> record full_trace.txt
Recording to full_trace.txt

# Step through many steps at once (all will be recorded)
pp> step 1000

# Stop recording
pp> stoprecord
Recording stopped
```

### 2. Recording Specific Macro Expansion
```bash
# Set a breakpoint on the macro you want to trace
pp> break call STRINGIFY
pp> record stringify_expansion.txt
Recording to stringify_expansion.txt

# Continue until breakpoint hits
pp> continue

# Step through the expansion (all steps recorded)
pp (called)> step 100

# Stop recording
pp> stoprecord
Recording stopped
```

### 3. Using with the expand Command
```bash
# Start recording before expanding
pp> record macro_analysis.txt
Recording to macro_analysis.txt

# Expand a macro interactively
pp> expand NESTED_MACRO(test)

# In the nested prompt, step through
pp [NESTED_MACRO(test)]> step 50

# Exit nested prompt
pp [NESTED_MACRO(test)]> quit

# Stop recording
pp> stoprecord
Recording stopped
```

### 4. Quick Recording Session
```bash
# Using shortcuts for faster workflow
pp> rec quick.txt
Recording to quick.txt
pp> s 500     # step 500 times
pp> sr        # stop recording using shortcut
Recording stopped
```

## Output Format

The recording file will contain entries like:

```
=== PPSTEP TRACE ===
Started: Fri Jan 10 10:30:45 2025
===================

[CALL] STRINGIFY(hello) // Args: hello
[EXPANDED] STRINGIFY(hello) => STRINGIFY_IMPL(hello)
[CALL] STRINGIFY_IMPL(hello) // Args: hello
[EXPANDED] STRINGIFY_IMPL(hello) => #hello
[RESCANNED] #hello => "hello" // Caused by: STRINGIFY_IMPL(hello)
[RESCANNED] STRINGIFY_IMPL(hello) => "hello" // Caused by: STRINGIFY(hello)
[LEXED] "hello"

=== END OF TRACE ===
```

## Command Summary

| Command | Shortcut | Description |
|---------|----------|-------------|
| `record <file>` | `rec <file>` | Start recording to file |
| `stoprecord` | `sr` | Stop recording |
| `status` | - | Show recording status |

## Key Features

1. **Works with all existing commands**: The recording happens transparently during any stepping operation (`step`, `step n`, `continue`, etc.)

2. **No performance impact when not recording**: Recording logic only executes when actively recording

3. **Clear output format**: Each event is clearly labeled with its type (CALL, EXPANDED, RESCANNED, LEXED)

4. **Context preservation**: Function-like macro calls include their arguments, rescans show what caused them

5. **File safety**: Automatically closes any previous recording before starting a new one

## Implementation Details

The recording functionality is implemented by:

1. Adding recording state to the `client` class in `client.hpp`
2. Integrating recording calls into existing event handlers (`on_lexed`, `on_expand_function`, `on_expand_object`, `on_expanded`, `on_rescanned`)
3. Adding command parsing for `record`, `stoprecord`, and `status` in `view.hpp`

The implementation is minimal and non-intrusive, preserving all existing ppstep functionality while adding the ability to capture preprocessing steps to a file.

## Note on Command Syntax

The stop recording command is `stoprecord` (one word, no hyphen) or its shortcut `sr`. This avoids parsing issues with hyphenated commands in the grammar.