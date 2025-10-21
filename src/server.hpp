#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <vector>
#include <string>
#include <type_traits>
#include <cstdlib>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "server_fwd.hpp"
#include "client.hpp"

namespace ppstep {
    template <class ContainerT>
    struct server_state {
        server_state() : expanding(), rescanning(), disable_printing(false) {}

        std::vector<ContainerT> expanding;
        std::vector<std::pair<ContainerT, ContainerT>> rescanning;
        bool disable_printing;  // Set to true after errors to prevent crashes
    };

    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(server_state<ContainerT>& state, client<TokenT, ContainerT>& sink, bool debug = false) : state(&state), sink(&sink), debug(debug), evaluating_conditional(false), fatal_error_occurred(false), main_input_file()  {}

        ~server() {}

        inline bool should_skip_token(TokenT const& token) {
            return IS_CATEGORY(token, boost::wave::WhiteSpaceTokenType)
                    || IS_CATEGORY(token, boost::wave::EOFTokenType)
                    || (boost::wave::token_id(token) == boost::wave::T_PLACEMARKER)
                    || !token.is_valid();
        }

        inline ContainerT sanitize(ContainerT const& tokens) {
            auto acc = ContainerT();
            for (auto const& token : tokens) {
                if (should_skip_token(token)) continue;
                acc.push_back(token);
            }
            return acc;
        }
        
        // New method that preserves whitespace for recording
        inline ContainerT preserve_whitespace(ContainerT const& tokens) {
            auto acc = ContainerT();
            for (auto const& token : tokens) {
                // Only skip EOF, placemarkers, and invalid tokens
                if (IS_CATEGORY(token, boost::wave::EOFTokenType)
                    || (boost::wave::token_id(token) == boost::wave::T_PLACEMARKER)
                    || !token.is_valid()) {
                    continue;
                }
                // Keep whitespace tokens for recording
                acc.push_back(token);
            }
            return acc;
        }

        template <typename ContextT, typename IteratorT>
        bool expanding_function_like_macro(
                ContextT& ctx,
                TokenT const& macrodef, std::vector<TokenT> const& formal_args,
                ContainerT const& definition,
                TokenT const& macrocall, std::vector<ContainerT> const& arguments,
                IteratorT const& seqstart, IteratorT const& seqend) {
            // CRITICAL: Don't record anything after error - tokens may be corrupted
            if (evaluating_conditional || fatal_error_occurred || state->disable_printing) return false;

            try {
                auto sanitized_arguments = std::vector<ContainerT>();
                for (auto const& arg_container : arguments) {
                    sanitized_arguments.push_back(sanitize(arg_container));
                }
                
                // Keep original with whitespace for recording
                auto preserved_arguments = std::vector<ContainerT>();
                for (auto const& arg_container : arguments) {
                    preserved_arguments.push_back(preserve_whitespace(arg_container));
                }

                auto full_call = ContainerT(seqstart, seqend);
                {
                    full_call.push_front(macrocall);
                    full_call.push_back(*seqend);
                    full_call = sanitize(full_call);
                }
                
                // Create preserved version for recording
                auto full_call_preserved = ContainerT(seqstart, seqend);
                {
                    full_call_preserved.push_front(macrocall);
                    full_call_preserved.push_back(*seqend);
                    full_call_preserved = preserve_whitespace(full_call_preserved);
                }
                
                if (!debug) {
                    sink->on_expand_function(ctx, macrodef, sanitized_arguments, full_call, preserved_arguments, full_call_preserved);
                } else {
                    std::cout << "F: ";
                    print_token_container(std::cout, full_call) << std::endl;
                }

                state->expanding.push_back(full_call);
            } catch (...) {
                // Any exception in hook processing means corrupted state
                fatal_error_occurred = true;
                state->disable_printing = true;
            }

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            // CRITICAL: Don't record anything after error - tokens may be corrupted
            if (evaluating_conditional || fatal_error_occurred || state->disable_printing) return false;
            
            try {
                if (!debug) {
                    sink->on_expand_object(ctx, macrocall);
                } else {
                    std::cout << "O: ";
                    print_token(std::cout, macrocall) << std::endl;
                }

                state->expanding.push_back({macrocall});
            } catch (...) {
                fatal_error_occurred = true;
                state->disable_printing = true;
            }
            
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            // CRITICAL: Don't record anything after error - tokens may be corrupted
            if (evaluating_conditional || fatal_error_occurred || state->disable_printing) return;

            try {
                auto const& initial = *(state->expanding.rbegin());
                
                if (!debug) {
                     // Pass both sanitized and preserved versions
                     sink->on_expanded(ctx, sanitize(initial), sanitize(result), 
                                      preserve_whitespace(initial), preserve_whitespace(result));
                } else {
                    std::cout << "E: ";
                    print_token_container(std::cout, sanitize(initial)) << " -> ";
                    print_token_container(std::cout, sanitize(result)) << std::endl;
                }

                state->rescanning.push_back({initial, result});
                state->expanding.pop_back();
            } catch (...) {
                fatal_error_occurred = true;
                state->disable_printing = true;
            }
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            // CRITICAL: Don't record anything after error - tokens may be corrupted
            if (evaluating_conditional || fatal_error_occurred || state->disable_printing) return;

            try {
                auto const& [cause, initial] = *(state->rescanning.rbegin());

                if (!debug) {
                    // Pass both sanitized and preserved versions
                    sink->on_rescanned(ctx, sanitize(cause), sanitize(initial), sanitize(result),
                                      preserve_whitespace(cause), preserve_whitespace(initial), preserve_whitespace(result));
                } else {
                    std::cout << "R: ";
                    print_token_container(std::cout, sanitize(initial)) << " -> ";
                    print_token_container(std::cout, sanitize(result)) << std::endl;
                }

                state->rescanning.pop_back();
            } catch (...) {
                fatal_error_occurred = true;
                state->disable_printing = true;
            }
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            if (fatal_error_occurred || state->disable_printing) return false;
            
            auto directive_id = boost::wave::token_id(directive);
            switch (directive_id) {
                case boost::wave::T_PP_IF:
                case boost::wave::T_PP_ELIF:
                case boost::wave::T_PP_IFDEF:
                case boost::wave::T_PP_IFNDEF: {
                    evaluating_conditional = true;
                    break;
                }
                default:
                    break;
            }
            return false;
        }
        
        template <typename ContextT>
        bool evaluated_conditional_expression(ContextT const& ctx, TokenT const& directive, ContainerT const& expression, bool expression_value) {
            if (fatal_error_occurred || state->disable_printing) return false;
            
            evaluating_conditional = false;
            return false;
        }
        
        template <typename ContextT, typename ParametersT, typename DefinitionT>
        void defined_macro(ContextT const& ctx, TokenT const& macro_name, bool is_functionlike, ParametersT const& parameters,
                           DefinitionT const& definition, bool is_predefined) {
            
        }
        
        template <typename ContextT>
        void undefined_macro(ContextT const& ctx, TokenT const& macro_name) {
            
        }

        // Hook to handle unknown directives (like #pragma GCC)
        template <typename ContextT, typename ContainerT2>
        bool found_unknown_directive(ContextT const& ctx, 
                                    ContainerT2 const& line, 
                                    ContainerT2& pending) {
            if (fatal_error_occurred || state->disable_printing) return false;
            
            // Check if this is a GCC-specific pragma or other problematic directive
            if (!line.empty()) {
                auto it = line.begin();
                std::string first_token;
                
                // Skip whitespace and get the first meaningful token
                while (it != line.end()) {
                    if (!IS_CATEGORY(*it, boost::wave::WhiteSpaceTokenType)) {
                        first_token = (*it).get_value().c_str();
                        break;
                    }
                    ++it;
                }
                
                // Handle GCC-specific pragmas
                if (first_token == "pragma" || first_token == "#pragma") {
                    // Look for the next token to see if it's GCC-related
                    ++it;
                    while (it != line.end()) {
                        if (!IS_CATEGORY(*it, boost::wave::WhiteSpaceTokenType)) {
                            std::string pragma_type = (*it).get_value().c_str();
                            if (pragma_type == "GCC" || pragma_type == "gcc") {
                                // Skip this pragma entirely
                                pending.clear();
                                if (debug) {
                                    std::cout << "Skipping GCC pragma" << std::endl;
                                }
                                return true; // We've handled it, don't process further
                            }
                            break;
                        }
                        ++it;
                    }
                }
                
                // Handle other problematic directives
                if (first_token == "warning" || first_token == "error") {
                    // These might be GCC-specific warning/error directives
                    pending.clear();
                    if (debug) {
                        std::cout << "Skipping compiler-specific directive: " << first_token << std::endl;
                    }
                    return true;
                }
            }
            
            return false; // Let Wave handle it normally
        }

        template <typename ContextT>
        void lexed_token(ContextT& ctx, TokenT const& result) {
            if (should_skip_token(result)) return;
            
            // Don't process hooks if fatal error, but don't crash either
            if (fatal_error_occurred || state->disable_printing) return;

            try {
                if (!debug) {
                    sink->on_lexed(ctx, result);
                } else {
                    std::cout << "L: ";
                    print_token(std::cout, result) << std::endl;
                }
            } catch (...) {
                // Ignore errors during error recovery
            }
        }
        
        // Dump full error context to log file
        template <typename ContextT, typename ExceptionT>
        void dump_error_to_log(ContextT& ctx, ExceptionT const& e) {
            // Get timestamp for log filename
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream log_filename;
            log_filename << "ppstep_error_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
            
            std::ofstream log(log_filename.str());
            if (!log.is_open()) {
                std::cerr << "ERROR: Could not create log file: " << log_filename.str() << std::endl;
                return;
            }
            
            // Write header
            log << "========================================" << std::endl;
            log << "PPSTEP PREPROCESSING ERROR LOG" << std::endl;
            log << "========================================" << std::endl;
            log << "Timestamp: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
            log << std::endl;
            
            // Extract and write error information
            std::string error_msg = "<unknown>";
            std::string file = "<unknown>";
            int line = 0;
            int column = 0;
            int severity = boost::wave::util::severity_fatal;
            
            try { error_msg = e.description(); } catch (...) {
                try { error_msg = e.what(); } catch (...) {}
            }
            
            try { file = e.file_name(); } catch (...) {
                try {
                    auto pos = ctx.get_main_pos();
                    file = std::string(pos.get_file().begin(), pos.get_file().end());
                } catch (...) {}
            }
            
            try { line = e.line_no(); } catch (...) {
                try {
                    auto pos = ctx.get_main_pos();
                    line = pos.get_line();
                } catch (...) {}
            }
            
            try {
                auto pos = ctx.get_main_pos();
                column = pos.get_column();
            } catch (...) {}
            
            try { severity = e.get_severity(); } catch (...) {}
            
            // Write error details
            log << "ERROR DETAILS:" << std::endl;
            log << "  Severity: ";
            switch (severity) {
                case boost::wave::util::severity_remark: log << "Remark"; break;
                case boost::wave::util::severity_warning: log << "Warning"; break;
                case boost::wave::util::severity_error: log << "Error"; break;
                case boost::wave::util::severity_fatal: log << "Fatal"; break;
                default: log << "Unknown"; break;
            }
            log << " (" << severity << ")" << std::endl;
            log << "  Message: " << error_msg << std::endl;
            log << std::endl;
            
            log << "LOCATION:" << std::endl;
            log << "  File: " << file << std::endl;
            log << "  Line: " << line << std::endl;
            log << "  Column: " << column << std::endl;
            log << std::endl;
            
            // Try to get context information
            log << "CONTEXT:" << std::endl;
            try {
                auto pos = ctx.get_main_pos();
                log << "  Current file: " << std::string(pos.get_file().begin(), pos.get_file().end()) << std::endl;
                log << "  Current line: " << pos.get_line() << std::endl;
                log << "  Current column: " << pos.get_column() << std::endl;
            } catch (...) {
                log << "  (Unable to retrieve context position)" << std::endl;
            }
            log << std::endl;
            
            // Write state information
            log << "PREPROCESSING STATE:" << std::endl;
            log << "  Expanding stack depth: " << state->expanding.size() << std::endl;
            log << "  Rescanning stack depth: " << state->rescanning.size() << std::endl;
            log << "  Evaluating conditional: " << (evaluating_conditional ? "yes" : "no") << std::endl;
            log << std::endl;

            // Dump the actual macro expansion stack
            if (!state->expanding.empty()) {
                log << "MACRO EXPANSION STACK (what was being expanded):" << std::endl;
                int level = 0;
                for (auto it = state->expanding.rbegin(); it != state->expanding.rend(); ++it, ++level) {
                    log << "  Level " << level << ": ";
                    for (auto const& token : *it) {
                        log << token.get_value().c_str();
                    }
                    log << std::endl;
                }
                log << std::endl;
            }

            // Dump the rescanning stack
            if (!state->rescanning.empty()) {
                log << "RESCANNING STACK:" << std::endl;
                int level = 0;
                for (auto it = state->rescanning.rbegin(); it != state->rescanning.rend(); ++it, ++level) {
                    log << "  Level " << level << " - Cause: ";
                    for (auto const& token : it->first) {
                        log << token.get_value().c_str();
                    }
                    log << std::endl;
                    log << "  Level " << level << " - Result: ";
                    for (auto const& token : it->second) {
                        log << token.get_value().c_str();
                    }
                    log << std::endl;
                }
                log << std::endl;
            }
            
            // Write exception type info
            log << "EXCEPTION INFO:" << std::endl;
            log << "  Type: " << typeid(e).name() << std::endl;
            try {
                log << "  What: " << e.what() << std::endl;
            } catch (...) {
                log << "  What: (unable to retrieve)" << std::endl;
            }
            log << std::endl;
            
            log << "========================================" << std::endl;
            log << "END OF ERROR LOG" << std::endl;
            log << "========================================" << std::endl;
            
            log.close();
            
            // Print log file location to stderr
            std::cerr << "Full error context written to: " << log_filename.str() << std::endl;
        }
        
        // Handle ALL exceptions - warnings ignored, errors logged and thrown
        template <typename ContextT, typename ExceptionT>
        bool throw_exception(ContextT& ctx, ExceptionT const& e) {
            int severity = boost::wave::util::severity_fatal;

            try {
                severity = e.get_severity();
            } catch (...) {
                severity = boost::wave::util::severity_fatal;
            }

            // Warnings: silently suppress
            if (severity == boost::wave::util::severity_remark ||
                severity == boost::wave::util::severity_warning) {
                return false;
            }

            // ERRORS: Check if this is in the EXACT input file
            std::string error_file;

            try {
                error_file = e.file_name();
            } catch (...) {}

            // If error is NOT in the main input file, skip it
            if (main_input_file.empty() || error_file != main_input_file) {
                return false;  // Suppress error in included files
            }

            // Error IS in the main input file - log and throw
            state->disable_printing = true;
            fatal_error_occurred = true;
            dump_error_to_log(ctx, e);

            // Return TRUE = throw to ppstep.cpp which will exit
            return true;
            }

            // Return TRUE = throw to ppstep.cpp which will exit
            return true;
        }

        template <typename ContextT>
        void start(ContextT& ctx) {
            // Store the main input file for error filtering
            try {
                auto pos = ctx.get_main_pos();
                main_input_file = std::string(pos.get_file().begin(), pos.get_file().end());
            } catch (...) {}

            if (debug) return;

            sink->on_start(ctx);
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            if (debug) return;
            
            // CRITICAL: Don't try to print event history if tokens are corrupted
            if (state->disable_printing) {
                std::cerr << "\n⚠️  Preprocessing stopped due to error - output may be incomplete" << std::endl;
                return;
            }

            sink->on_complete(ctx);
        }

        server_state<ContainerT>* state;
        client<TokenT, ContainerT>* sink;
        bool debug;

        unsigned int conditional_nesting;
        bool evaluating_conditional;
        bool fatal_error_occurred;
        std::string main_input_file;
    };
}

#endif // PPSTEP_SERVER_HPP
