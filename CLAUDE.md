# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ppstep is an interactive debugger for C/C++ preprocessor macros. It allows users to visually single-step through macro expansion, set breakpoints on macros, and interactively evaluate preprocessor directives. Built on Boost.Wave, ppstep helps developers debug and understand complex macro systems.

## Build Commands

### Standard Build
```bash
cmake . && make
```

### Build with AddressSanitizer (for debugging memory issues)
```bash
./build_with_asan.sh
```

Or manually:
```bash
mkdir -p build_asan
cd build_asan
cmake .. -DENABLE_ASAN=ON
make -j$(nproc)
```

### Custom Boost Configuration
If using a custom Boost installation:
```bash
cmake . \
  -DBOOST_ROOT=/path/to/boost \
  -DBOOST_INCLUDEDIR=/path/to/boost/include \
  -DBOOST_LIBRARYDIR=/path/to/boost/lib
make
```

### Running the Binary
```bash
./ppstep your-source-file.c

# With common preprocessor flags
./ppstep source.c -I./include -DMACRO_NAME -UOTHER_MACRO

# With AddressSanitizer
ASAN_OPTIONS=detect_leaks=0:halt_on_error=0:symbolize=1 ./build_asan/ppstep source.c
```

## Architecture

### Core Components

**ppstep.cpp** - Main entry point that:
- Parses command-line arguments (include paths, macro definitions)
- Reads the input source file
- Sets up Boost.Wave context with C++20 language support
- Drives the preprocessing loop through server/client interaction
- Installs crash handlers for diagnostics

**server.hpp** - Boost.Wave policy hook (preprocessing event callbacks):
- `expanding_function_like_macro()` - Tracks macro expansion entry
- `expanded_macro()` - Tracks macro expansion completion
- `rescanned_macro()` - Tracks token rescanning
- `lexed_token()` - Handles each preprocessed token
- Integrates with the client to provide interactive debugging
- Updates crash context during macro operations

**client.hpp** - Interactive REPL and visualization system:
- Manages the debugging prompt and command processing
- Implements commands: `step`, `continue`, `break`, `backtrace`, `forwardtrace`, `expand`, `quit`
- Tracks preprocessing history with configurable limits (MAX_HISTORY_SIZE)
- Handles breakpoints on macro calls and expansions
- Provides color-coded visual output for token changes

**view.hpp** - Terminal UI and token rendering:
- Handles ANSI color codes for visual feedback
- Renders token sequences with proper formatting
- Implements the visual "diff" highlighting for macro changes

**crash_handler.hpp** - Signal-safe crash detection system:
- Catches SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS
- Thread-local context tracking (file, line, macro, operation, token)
- Writes detailed crash dumps to `ppstep_crash.log`
- Provides backtrace and diagnostic information when Boost.Wave crashes
- Signal-safe design (no allocations or locks in handlers)

**utils.hpp** - Shared utilities and helper functions

### Type System

The codebase uses several key Boost.Wave types:

```cpp
using token_type = boost::wave::cpplexer::lex_token<>;
using token_sequence_type = std::list<token_type, boost::fast_pool_allocator<token_type>>;
using lex_iterator_type = boost::wave::cpplexer::lex_iterator<token_type>;
using context_type = boost::wave::context<...>;
```

The `server_state` holds the current preprocessing state (expanding/rescanning stacks), while the `server` receives callbacks from Boost.Wave and forwards them to the `client` for interactive processing.

### Boost.Wave Integration

ppstep is fundamentally a Boost.Wave application with a custom context policy (`server`). Wave invokes the server's hook functions during preprocessing:

1. Wave tokenizes input
2. For each macro expansion, Wave calls `expanding_function_like_macro()` or `expanding_object_like_macro()`
3. Wave expands the macro
4. Wave calls `expanded_macro()` with the result
5. Wave rescans tokens and calls `rescanned_macro()`
6. For each final token, Wave calls `lexed_token()`

The server forwards these events to the client, which presents them interactively.

## Memory and Crash Handling

### AddressSanitizer Build

The ENABLE_ASAN option configures comprehensive debugging:
- AddressSanitizer for memory corruption detection
- UndefinedBehaviorSanitizer for UB detection
- Full debug symbols (-g3)
- No optimization (-O0)
- No function inlining for clearer stack traces

Run with: `ASAN_OPTIONS=detect_leaks=0:halt_on_error=0:symbolize=1 ./build_asan/ppstep file.c`

### Crash Detection

ppstep includes signal-safe crash handlers that capture:
- Source file location (file, line, column)
- Macro being processed
- Current preprocessing operation
- Last valid token
- Macro expansion depth
- Full backtrace

Crash logs are written to `ppstep_crash.log` with actionable diagnostic information.

**Important**: The crash handler cannot prevent Boost.Wave crashes, but makes them diagnosable by providing exact context of what was being processed when the crash occurred.

## Known Issues

### Boost.Wave Segfaults

Boost.Wave can crash (rather than gracefully error) on certain malformed inputs or deeply nested macro expansions. The crash handler system provides diagnostic information to identify the problematic macro and location, but cannot prevent the crash itself.

When this occurs:
1. Check the crash output for the macro name and source location
2. Examine the macro definition for syntax errors
3. Try simplifying complex macro expansions
4. Consider building with ASan for additional memory diagnostics
5. Review `ppstep_crash.log` for full context

### History Memory Management

The client maintains preprocessing history with a configurable limit (MAX_HISTORY_SIZE = 1000 events, trimmed to HISTORY_TRIM_SIZE = 800 when exceeded) to prevent OOM on large files with extensive macro processing.

## Dependencies

- C++17 compiler (GCC 5+, Clang 5+)
- Boost libraries (relatively recent version recommended):
  - boost_filesystem
  - boost_program_options
  - boost_thread
  - boost_wave
- linenoise (included in external/)

## Interactive Commands

Users interact with ppstep through a REPL prompt:
- `step` / `s` - Step to next preprocessing event
- `continue` / `c` - Continue until breakpoint or completion
- `break call MACRO` / `bc MACRO` - Break when macro is called
- `break expand MACRO` / `be MACRO` - Break when macro finishes expanding
- `delete call MACRO` / `dc MACRO` - Delete call breakpoint
- `backtrace` / `bt` - Show pending macro expansion stack
- `forwardtrace` / `ft` - Show anticipated macro rescans
- `expand MACRO(args)` / `e MACRO(args)` - Interactively expand a macro
- `#define`, `#undef`, `#include` - Execute preprocessor directives mid-session
- `quit` / `q` - Exit
