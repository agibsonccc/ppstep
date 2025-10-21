#ifndef PPSTEP_CRASH_HANDLER_HPP
#define PPSTEP_CRASH_HANDLER_HPP

#include <csignal>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

namespace ppstep {
    namespace crash_handler_detail {
        // Thread-local storage for crash context
        struct crash_context {
            const char* filename;
            int line;
            int column;
            const char* macro_name;
            const char* last_token;
            const char* operation;
            int macro_depth;
            
            // Expansion chain tracking
            static constexpr int MAX_CHAIN_DEPTH = 32;
            const char* expansion_chain[MAX_CHAIN_DEPTH];
            const char* expansion_types[MAX_CHAIN_DEPTH];  // "ENTRY", "INNER", "NEXT", etc.

            crash_context() : filename(nullptr), line(0), column(0),
                            macro_name(nullptr), last_token(nullptr),
                            operation(nullptr), macro_depth(0) {
                for (int i = 0; i < MAX_CHAIN_DEPTH; ++i) {
                    expansion_chain[i] = nullptr;
                    expansion_types[i] = nullptr;
                }
            }
        };
        
        thread_local crash_context g_crash_context;
        
        // Signal-safe string write
        inline void safe_write(int fd, const char* str) {
            if (str) {
                write(fd, str, strlen(str));
            }
        }
        
        // Signal-safe integer write
        inline void safe_write_int(int fd, int value) {
            char buffer[32];
            int len = snprintf(buffer, sizeof(buffer), "%d", value);
            if (len > 0) {
                write(fd, buffer, len);
            }
        }
        
        // Signal handler
        inline void signal_handler(int sig) {
            const int STDERR = 2;
            
            safe_write(STDERR, "\n╔════════════════════════════════════════════╗\n");
            safe_write(STDERR, "║     PPSTEP CRASH HANDLER - ");
            
            switch (sig) {
                case SIGSEGV: safe_write(STDERR, "SEGFAULT"); break;
                case SIGABRT: safe_write(STDERR, "ABORT"); break;
                case SIGILL:  safe_write(STDERR, "ILLEGAL INSTRUCTION"); break;
                case SIGFPE:  safe_write(STDERR, "FP EXCEPTION"); break;
#if defined(__linux__) || defined(__APPLE__)
                case SIGBUS:  safe_write(STDERR, "BUS ERROR"); break;
#endif
                default:      safe_write(STDERR, "UNKNOWN");
            }
            
            safe_write(STDERR, "       ║\n");
            safe_write(STDERR, "╚════════════════════════════════════════════╝\n\n");
            
            // Location info
            if (g_crash_context.filename) {
                safe_write(STDERR, "📍 LOCATION: ");
                safe_write(STDERR, g_crash_context.filename);
                safe_write(STDERR, ":");
                safe_write_int(STDERR, g_crash_context.line);
                safe_write(STDERR, ":");
                safe_write_int(STDERR, g_crash_context.column);
                safe_write(STDERR, "\n");
            }
            
            // Macro info
            if (g_crash_context.macro_name) {
                safe_write(STDERR, "🎯 MACRO: ");
                safe_write(STDERR, g_crash_context.macro_name);
                safe_write(STDERR, "\n");
            }
            
            // Token info
            if (g_crash_context.last_token) {
                safe_write(STDERR, "🔤 LAST TOKEN: ");
                safe_write(STDERR, g_crash_context.last_token);
                safe_write(STDERR, "\n");
            }
            
            // Operation info
            if (g_crash_context.operation) {
                safe_write(STDERR, "🔧 OPERATION: ");
                safe_write(STDERR, g_crash_context.operation);
                safe_write(STDERR, "\n");
            }
            
            // Depth info
            if (g_crash_context.macro_depth > 0) {
                safe_write(STDERR, "📊 DEPTH: ");
                safe_write_int(STDERR, g_crash_context.macro_depth);
                safe_write(STDERR, " levels deep\n");
            }
            
#if defined(__linux__) || defined(__APPLE__)
            // Backtrace
            safe_write(STDERR, "\n=== BACKTRACE ===\n");
            void* buffer[64];
            int size = backtrace(buffer, 64);
            backtrace_symbols_fd(buffer, size, STDERR);
            safe_write(STDERR, "=================\n");
#endif
            
            // Try to write crash log file (may fail in signal handler)
            FILE* log = fopen("ppstep_crash.log", "w");
            if (log) {
                fprintf(log, "PPSTEP CRASH REPORT\n");
                fprintf(log, "===================\n\n");
                
                time_t now = time(nullptr);
                fprintf(log, "Time: %s\n", ctime(&now));
                fprintf(log, "Signal: %d\n\n", sig);
                
                if (g_crash_context.filename) {
                    fprintf(log, "Location: %s:%d:%d\n", 
                           g_crash_context.filename,
                           g_crash_context.line,
                           g_crash_context.column);
                }
                
                if (g_crash_context.macro_name) {
                    fprintf(log, "Macro: %s\n", g_crash_context.macro_name);
                }
                
                if (g_crash_context.last_token) {
                    fprintf(log, "Last Token: %s\n", g_crash_context.last_token);
                }
                
                if (g_crash_context.operation) {
                    fprintf(log, "Operation: %s\n", g_crash_context.operation);
                }
                
                if (g_crash_context.macro_depth > 0) {
                    fprintf(log, "Macro Depth: %d\n", g_crash_context.macro_depth);
                }
                
#if defined(__linux__) || defined(__APPLE__)
                fprintf(log, "\nBacktrace:\n");
                void* buffer[64];
                int size = backtrace(buffer, 64);
                char** symbols = backtrace_symbols(buffer, size);
                if (symbols) {
                    for (int i = 0; i < size; i++) {
                        fprintf(log, "  %s\n", symbols[i]);
                    }
                    free(symbols);
                }
#endif
                
                fclose(log);
                safe_write(STDERR, "\n💾 Crash log written to: ppstep_crash.log\n");
            }
            
            // Re-raise signal with default handler
            signal(sig, SIG_DFL);
            raise(sig);
        }
    }
    
    // Public API for updating crash context
    struct crash_context_guard {
        static void set_file_position(const char* filename, int line, int column) {
            crash_handler_detail::g_crash_context.filename = filename;
            crash_handler_detail::g_crash_context.line = line;
            crash_handler_detail::g_crash_context.column = column;
        }
        
        // Convenience method used by ppstep.cpp
        static void set_file(const char* filename, int line, int column) {
            set_file_position(filename, line, column);
        }
        
        static void set_token(const char* token) {
            crash_handler_detail::g_crash_context.last_token = token;
        }
        
        static void set_operation(const char* op) {
            crash_handler_detail::g_crash_context.operation = op;
        }
        
        static void set_macro(const char* macro_name) {
            crash_handler_detail::g_crash_context.macro_name = macro_name;
        }
        
        static void enter_macro_expansion(const char* macro_name = nullptr, const char* expansion_type = "EXPAND") {
            if (macro_name) {
                crash_handler_detail::g_crash_context.macro_name = macro_name;
            }

            int depth = crash_handler_detail::g_crash_context.macro_depth;
            if (depth < crash_handler_detail::crash_context::MAX_CHAIN_DEPTH) {
                crash_handler_detail::g_crash_context.expansion_chain[depth] = macro_name;
                crash_handler_detail::g_crash_context.expansion_types[depth] = expansion_type;
            }

            crash_handler_detail::g_crash_context.macro_depth++;
        }
        
        static void exit_macro_expansion() {
            if (crash_handler_detail::g_crash_context.macro_depth > 0) {
                crash_handler_detail::g_crash_context.macro_depth--;
            }
            if (crash_handler_detail::g_crash_context.macro_depth == 0) {
                crash_handler_detail::g_crash_context.macro_name = nullptr;
            }
        }
        
        static void clear() {
            crash_handler_detail::g_crash_context = crash_handler_detail::crash_context();
        }
    };
    
    // RAII guard for macro expansion tracking
    struct macro_expansion_guard {
        macro_expansion_guard(const char* macro_name) {
            crash_context_guard::enter_macro_expansion(macro_name);
        }
        
        ~macro_expansion_guard() {
            crash_context_guard::exit_macro_expansion();
        }
        
        macro_expansion_guard(const macro_expansion_guard&) = delete;
        macro_expansion_guard& operator=(const macro_expansion_guard&) = delete;
    };
    
    // Install crash handlers
    inline void install_crash_handlers() {
        std::signal(SIGSEGV, crash_handler_detail::signal_handler);
        std::signal(SIGABRT, crash_handler_detail::signal_handler);
        std::signal(SIGILL, crash_handler_detail::signal_handler);
        std::signal(SIGFPE, crash_handler_detail::signal_handler);
        
#if defined(__linux__) || defined(__APPLE__)
        std::signal(SIGBUS, crash_handler_detail::signal_handler);
#endif
    }
}

#endif // PPSTEP_CRASH_HANDLER_HPP
