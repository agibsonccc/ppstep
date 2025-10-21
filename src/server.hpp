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
#include "crash_handler.hpp"

namespace ppstep {
    // Global diagnostic file stream that stays open
    static std::ofstream g_expansion_trace("ppstep_expansion_trace.log", std::ios::out | std::ios::trunc);
    static int g_expansion_depth = 0;
    
    template <class ContainerT>
    struct server_state {
        server_state() : expanding(), rescanning(), disable_printing(false) {}

        std::vector<ContainerT> expanding;
        std::vector<std::pair<ContainerT, ContainerT>> rescanning;
        bool disable_printing;
    };

    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(server_state<ContainerT>& state, client<TokenT, ContainerT>& sink, bool debug = false, bool continue_on_error = false) 
            : state(&state), sink(&sink), debug(debug), continue_on_error(continue_on_error), evaluating_conditional(false), fatal_error_occurred(false), main_input_file()  {
            // Initialize trace file
            if (g_expansion_trace.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                g_expansion_trace << "========================================\n";
                g_expansion_trace << "PPSTEP EXPANSION TRACE\n";
                g_expansion_trace << "Started: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\n";
                g_expansion_trace << "========================================\n\n";
                g_expansion_trace.flush();
            }
        }

        ~server() {
            if (g_expansion_trace.is_open()) {
                g_expansion_trace << "\n========================================\n";
                g_expansion_trace << "END OF TRACE\n";
                g_expansion_trace << "========================================\n";
                g_expansion_trace.close();
            }
        }

        inline bool should_skip_token(TokenT const& token) {
            try {
                return IS_CATEGORY(token, boost::wave::WhiteSpaceTokenType)
                        || IS_CATEGORY(token, boost::wave::EOFTokenType)
                        || (boost::wave::token_id(token) == boost::wave::T_PLACEMARKER)
                        || !token.is_valid();
            } catch (...) {
                return true;
            }
        }

        inline ContainerT sanitize(ContainerT const& tokens) {
            auto acc = ContainerT();
            try {
                for (auto const& token : tokens) {
                    if (should_skip_token(token)) continue;
                    acc.push_back(token);
                }
            } catch (...) {}
            return acc;
        }
        
        inline ContainerT preserve_whitespace(ContainerT const& tokens) {
            auto acc = ContainerT();
            try {
                for (auto const& token : tokens) {
                    try {
                        if (IS_CATEGORY(token, boost::wave::EOFTokenType)
                            || (boost::wave::token_id(token) == boost::wave::T_PLACEMARKER)
                            || !token.is_valid()) {
                            continue;
                        }
                        acc.push_back(token);
                    } catch (...) {
                        continue;
                    }
                }
            } catch (...) {}
            return acc;
        }

        template <typename ContextT, typename IteratorT>
        bool expanding_function_like_macro(
                ContextT& ctx,
                TokenT const& macrodef, std::vector<TokenT> const& formal_args,
                ContainerT const& definition,
                TokenT const& macrocall, std::vector<ContainerT> const& arguments,
                IteratorT const& seqstart, IteratorT const& seqend) {
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;

            try {
                crash_context_guard::set_operation("expanding function-like macro");
                
                // Store macro name
                static thread_local char macro_name_buffer[256];
                try {
                    auto val = macrocall.get_value();
                    size_t len = std::min(val.size(), sizeof(macro_name_buffer) - 1);
                    std::memcpy(macro_name_buffer, val.c_str(), len);
                    macro_name_buffer[len] = '\0';
                    crash_context_guard::set_macro(macro_name_buffer);
                } catch (...) {
                    macro_name_buffer[0] = '\0';
                }
                
                // CONTINUOUS TRACE LOGGING - write immediately and flush
                if (g_expansion_trace.is_open()) {
                    try {
                        std::string indent(g_expansion_depth * 2, ' ');
                        g_expansion_trace << indent << ">>> EXPANDING FUNCTION-LIKE: " << macro_name_buffer << "\n";
                        g_expansion_trace << indent << "    Depth: " << g_expansion_depth << "\n";
                        
                        try {
                            auto pos = ctx.get_main_pos();
                            g_expansion_trace << indent << "    Location: " 
                                            << std::string(pos.get_file().begin(), pos.get_file().end())
                                            << ":" << pos.get_line() << ":" << pos.get_column() << "\n";
                        } catch (...) {}
                        
                        g_expansion_trace << indent << "    Parameters (" << formal_args.size() << "): ";
                        for (size_t i = 0; i < formal_args.size(); ++i) {
                            try {
                                if (i > 0) g_expansion_trace << ", ";
                                g_expansion_trace << formal_args[i].get_value().c_str();
                            } catch (...) {
                                g_expansion_trace << "<ERR>";
                            }
                        }
                        g_expansion_trace << "\n";
                        
                        g_expansion_trace << indent << "    Arguments (" << arguments.size() << "):\n";
                        for (size_t i = 0; i < arguments.size(); ++i) {
                            g_expansion_trace << indent << "      [" << i << "] ";
                            try {
                                for (auto const& token : arguments[i]) {
                                    try {
                                        g_expansion_trace << token.get_value().c_str() << " ";
                                    } catch (...) {
                                        g_expansion_trace << "<ERR> ";
                                    }
                                }
                            } catch (...) {
                                g_expansion_trace << "<CORRUPTED>";
                            }
                            g_expansion_trace << "\n";
                        }
                        
                        g_expansion_trace << indent << "    Definition: ";
                        try {
                            int token_count = 0;
                            for (auto const& token : definition) {
                                try {
                                    g_expansion_trace << token.get_value().c_str() << " ";
                                    if (++token_count > 50) {
                                        g_expansion_trace << "... (truncated)";
                                        break;
                                    }
                                } catch (...) {
                                    g_expansion_trace << "<ERR> ";
                                }
                            }
                        } catch (...) {
                            g_expansion_trace << "<CORRUPTED>";
                        }
                        g_expansion_trace << "\n";
                        
                        g_expansion_trace.flush(); // CRITICAL: flush immediately so we have the log even if we crash
                    } catch (...) {}
                }
                
                g_expansion_depth++;

                // Determine expansion type
                const char* expansion_type = "ENTRY";
                if (macro_name_buffer[0] != '\0') {
                    if (strstr(macro_name_buffer, "_INNER") || strstr(macro_name_buffer, "_inner")) {
                        expansion_type = "INNER";
                    } else if (strstr(macro_name_buffer, "_NEXT") || strstr(macro_name_buffer, "_next")) {
                        expansion_type = "NEXT";
                    } else if (strstr(macro_name_buffer, "_IMPL") || strstr(macro_name_buffer, "_impl")) {
                        expansion_type = "IMPL";
                    } else if (state->expanding.size() > 0) {
                        expansion_type = "EXPAND";
                    }
                }
                crash_context_guard::enter_macro_expansion(macro_name_buffer, expansion_type);
                
                auto sanitized_arguments = std::vector<ContainerT>();
                try {
                    for (auto const& arg_container : arguments) {
                        sanitized_arguments.push_back(sanitize(arg_container));
                    }
                } catch (...) {}
                
                auto preserved_arguments = std::vector<ContainerT>();
                try {
                    for (auto const& arg_container : arguments) {
                        preserved_arguments.push_back(preserve_whitespace(arg_container));
                    }
                } catch (...) {}

                auto full_call = ContainerT();
                auto full_call_preserved = ContainerT();
                
                try {
                    full_call = ContainerT(seqstart, seqend);
                    full_call.push_front(macrocall);
                    full_call.push_back(*seqend);
                    full_call = sanitize(full_call);
                } catch (...) {
                    try {
                        full_call = ContainerT{macrocall};
                    } catch (...) {
                        g_expansion_depth--;
                        crash_context_guard::exit_macro_expansion();
                        return false;
                    }
                }
                
                try {
                    full_call_preserved = ContainerT(seqstart, seqend);
                    full_call_preserved.push_front(macrocall);
                    full_call_preserved.push_back(*seqend);
                    full_call_preserved = preserve_whitespace(full_call_preserved);
                } catch (...) {
                    full_call_preserved = full_call;
                }
                
                if (!debug) {
                    try {
                        sink->on_expand_function(ctx, macrodef, sanitized_arguments, full_call, preserved_arguments, full_call_preserved);
                    } catch (...) {}
                } else {
                    std::cout << "F: ";
                    print_token_container(std::cout, full_call) << std::endl;
                }

                try {
                    state->expanding.push_back(full_call);
                } catch (...) {
                    g_expansion_depth--;
                    crash_context_guard::exit_macro_expansion();
                    return false;
                }
                
            } catch (...) {
                g_expansion_depth--;
                if (!continue_on_error) {
                    fatal_error_occurred = true;
                    state->disable_printing = true;
                }
                crash_context_guard::exit_macro_expansion();
            }

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
            try {
                crash_context_guard::set_operation("expanding object-like macro");
                
                // Store macro name
                static thread_local char macro_name_buffer[256];
                try {
                    auto val = macrocall.get_value();
                    size_t len = std::min(val.size(), sizeof(macro_name_buffer) - 1);
                    std::memcpy(macro_name_buffer, val.c_str(), len);
                    macro_name_buffer[len] = '\0';
                    crash_context_guard::set_macro(macro_name_buffer);
                } catch (...) {
                    macro_name_buffer[0] = '\0';
                }
                
                // CONTINUOUS TRACE LOGGING - write immediately and flush
                if (g_expansion_trace.is_open()) {
                    try {
                        std::string indent(g_expansion_depth * 2, ' ');
                        g_expansion_trace << indent << ">>> EXPANDING OBJECT-LIKE: " << macro_name_buffer << "\n";
                        g_expansion_trace << indent << "    Depth: " << g_expansion_depth << "\n";
                        
                        try {
                            auto pos = ctx.get_main_pos();
                            g_expansion_trace << indent << "    Location: " 
                                            << std::string(pos.get_file().begin(), pos.get_file().end())
                                            << ":" << pos.get_line() << ":" << pos.get_column() << "\n";
                        } catch (...) {}
                        
                        g_expansion_trace << indent << "    Definition: ";
                        try {
                            int token_count = 0;
                            for (auto const& token : definition) {
                                try {
                                    g_expansion_trace << token.get_value().c_str() << " ";
                                    if (++token_count > 50) {
                                        g_expansion_trace << "... (truncated)";
                                        break;
                                    }
                                } catch (...) {
                                    g_expansion_trace << "<ERR> ";
                                }
                            }
                        } catch (...) {
                            g_expansion_trace << "<CORRUPTED>";
                        }
                        g_expansion_trace << "\n";
                        
                        g_expansion_trace.flush(); // CRITICAL: flush immediately
                    } catch (...) {}
                }
                
                g_expansion_depth++;
                
                // Determine expansion type
                const char* expansion_type = "ENTRY";
                if (macro_name_buffer[0] != '\0') {
                    if (strstr(macro_name_buffer, "_INNER") || strstr(macro_name_buffer, "_inner")) {
                        expansion_type = "INNER";
                    } else if (strstr(macro_name_buffer, "_NEXT") || strstr(macro_name_buffer, "_next")) {
                        expansion_type = "NEXT";
                    } else if (strstr(macro_name_buffer, "_IMPL") || strstr(macro_name_buffer, "_impl")) {
                        expansion_type = "IMPL";
                    } else if (state->expanding.size() > 0) {
                        expansion_type = "EXPAND";
                    }
                }
                crash_context_guard::enter_macro_expansion(macro_name_buffer, expansion_type);
                
                if (!debug) {
                    try {
                        sink->on_expand_object(ctx, macrocall);
                    } catch (...) {}
                } else {
                    std::cout << "O: ";
                    print_token(std::cout, macrocall) << std::endl;
                }

                try {
                    state->expanding.push_back({macrocall});
                } catch (...) {
                    g_expansion_depth--;
                    crash_context_guard::exit_macro_expansion();
                    return false;
                }
                
            } catch (...) {
                g_expansion_depth--;
                if (!continue_on_error) {
                    fatal_error_occurred = true;
                    state->disable_printing = true;
                }
                crash_context_guard::exit_macro_expansion();
            }
            
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            try {
                crash_context_guard::set_operation("macro expanded");
                
                // GUARD: Check if expanding stack is empty
                if (state->expanding.empty()) {
                    std::cerr << "⚠️  Warning: expanded_macro called with empty expanding stack" << std::endl;
                    return;
                }
                
                auto const& initial = *(state->expanding.rbegin());
                
                // Log completion
                if (g_expansion_trace.is_open()) {
                    try {
                        g_expansion_depth--;
                        std::string indent(g_expansion_depth * 2, ' ');
                        g_expansion_trace << indent << "<<< EXPANDED TO: ";
                        try {
                            int token_count = 0;
                            for (auto const& token : result) {
                                try {
                                    g_expansion_trace << token.get_value().c_str() << " ";
                                    if (++token_count > 30) {
                                        g_expansion_trace << "... (truncated)";
                                        break;
                                    }
                                } catch (...) {
                                    g_expansion_trace << "<ERR> ";
                                }
                            }
                        } catch (...) {
                            g_expansion_trace << "<CORRUPTED>";
                        }
                        g_expansion_trace << "\n";
                        g_expansion_trace.flush();
                    } catch (...) {}
                } else {
                    g_expansion_depth--;
                }
                
                if (!debug) {
                    try {
                        sink->on_expanded(ctx, sanitize(initial), sanitize(result), 
                                         preserve_whitespace(initial), preserve_whitespace(result));
                    } catch (...) {}
                } else {
                    std::cout << "E: ";
                    print_token_container(std::cout, sanitize(initial)) << " -> ";
                    print_token_container(std::cout, sanitize(result)) << std::endl;
                }

                try {
                    state->rescanning.push_back({initial, result});
                } catch (...) {}
                
                state->expanding.pop_back();
                crash_context_guard::exit_macro_expansion();
                crash_context_guard::set_operation("token processing");
                
            } catch (...) {
                g_expansion_depth--;
                if (!continue_on_error) {
                    fatal_error_occurred = true;
                    state->disable_printing = true;
                }
                crash_context_guard::exit_macro_expansion();
            }
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            try {
                crash_context_guard::set_operation("rescanning macro");
                
                // GUARD: Check if rescanning stack is empty
                if (state->rescanning.empty()) {
                    std::cerr << "⚠️  Warning: rescanned_macro called with empty rescanning stack" << std::endl;
                    return;
                }
                
                auto const& [cause, initial] = *(state->rescanning.rbegin());

                if (!debug) {
                    try {
                        sink->on_rescanned(ctx, sanitize(cause), sanitize(initial), sanitize(result),
                                          preserve_whitespace(cause), preserve_whitespace(initial), preserve_whitespace(result));
                    } catch (...) {}
                } else {
                    std::cout << "R: ";
                    print_token_container(std::cout, sanitize(initial)) << " -> ";
                    print_token_container(std::cout, sanitize(result)) << std::endl;
                }

                state->rescanning.pop_back();
                
            } catch (...) {
                if (!continue_on_error) {
                    fatal_error_occurred = true;
                    state->disable_printing = true;
                }
            }
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
            try {
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
            } catch (...) {}
            return false;
        }
        
        template <typename ContextT>
        bool evaluated_conditional_expression(ContextT const& ctx, TokenT const& directive, ContainerT const& expression, bool expression_value) {
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
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

        template <typename ContextT, typename ContainerT2>
        bool found_unknown_directive(ContextT const& ctx, 
                                    ContainerT2 const& line, 
                                    ContainerT2& pending) {
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
            try {
                if (!line.empty()) {
                    auto it = line.begin();
                    std::string first_token;
                    
                    while (it != line.end()) {
                        if (!IS_CATEGORY(*it, boost::wave::WhiteSpaceTokenType)) {
                            first_token = (*it).get_value().c_str();
                            break;
                        }
                        ++it;
                    }
                    
                    if (first_token == "pragma" || first_token == "#pragma") {
                        ++it;
                        while (it != line.end()) {
                            if (!IS_CATEGORY(*it, boost::wave::WhiteSpaceTokenType)) {
                                std::string pragma_type = (*it).get_value().c_str();
                                if (pragma_type == "GCC" || pragma_type == "gcc") {
                                    pending.clear();
                                    if (debug) {
                                        std::cout << "Skipping GCC pragma" << std::endl;
                                    }
                                    return true;
                                }
                                break;
                            }
                            ++it;
                        }
                    }
                    
                    if (first_token == "warning" || first_token == "error") {
                        pending.clear();
                        if (debug) {
                            std::cout << "Skipping compiler-specific directive: " << first_token << std::endl;
                        }
                        return true;
                    }
                }
            } catch (...) {}
            
            return false;
        }

        template <typename ContextT>
        void lexed_token(ContextT& ctx, TokenT const& result) {
            if (should_skip_token(result)) return;
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            try {
                if (!debug) {
                    try {
                        sink->on_lexed(ctx, result);
                    } catch (...) {}
                } else {
                    std::cout << "L: ";
                    print_token(std::cout, result) << std::endl;
                }
            } catch (...) {}
        }
        
        template <typename ContextT, typename ExceptionT>
        void dump_error_to_log(ContextT& ctx, ExceptionT const& e) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream log_filename;
            log_filename << "ppstep_error_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
            
            std::ofstream log(log_filename.str());
            if (!log.is_open()) {
                std::cerr << "ERROR: Could not create log file: " << log_filename.str() << std::endl;
                return;
            }
            
            log << "========================================" << std::endl;
            log << "PPSTEP PREPROCESSING ERROR LOG" << std::endl;
            log << "========================================" << std::endl;
            log << "Timestamp: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
            log << std::endl;
            
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
            
            log << "PREPROCESSING STATE:" << std::endl;
            log << "  Expanding stack depth: " << state->expanding.size() << std::endl;
            log << "  Rescanning stack depth: " << state->rescanning.size() << std::endl;
            log << "  Evaluating conditional: " << (evaluating_conditional ? "yes" : "no") << std::endl;
            log << std::endl;

            if (!state->expanding.empty()) {
                log << "MACRO EXPANSION STACK:" << std::endl;
                int level = 0;
                for (auto it = state->expanding.rbegin(); it != state->expanding.rend(); ++it, ++level) {
                    log << "  Level " << level << ": ";
                    try {
                        for (auto const& token : *it) {
                            try {
                                log << token.get_value().c_str();
                            } catch (...) {
                                log << "<CORRUPTED>";
                            }
                        }
                    } catch (...) {
                        log << "<CORRUPTED_CONTAINER>";
                    }
                    log << std::endl;
                }
                log << std::endl;
            }

            if (!state->rescanning.empty()) {
                log << "RESCANNING STACK:" << std::endl;
                int level = 0;
                for (auto it = state->rescanning.rbegin(); it != state->rescanning.rend(); ++it, ++level) {
                    log << "  Level " << level << " - Cause: ";
                    try {
                        for (auto const& token : it->first) {
                            try {
                                log << token.get_value().c_str();
                            } catch (...) {
                                log << "<CORRUPTED>";
                            }
                        }
                    } catch (...) {
                        log << "<CORRUPTED_CONTAINER>";
                    }
                    log << std::endl;
                    
                    log << "  Level " << level << " - Result: ";
                    try {
                        for (auto const& token : it->second) {
                            try {
                                log << token.get_value().c_str();
                            } catch (...) {
                                log << "<CORRUPTED>";
                            }
                        }
                    } catch (...) {
                        log << "<CORRUPTED_CONTAINER>";
                    }
                    log << std::endl;
                }
                log << std::endl;
            }
            
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
            
            std::cerr << "Full error context written to: " << log_filename.str() << std::endl;
        }
        
        template <typename ContextT, typename ExceptionT>
        bool throw_exception(ContextT& ctx, ExceptionT const& e) {
            int severity = boost::wave::util::severity_fatal;

            try {
                severity = e.get_severity();
            } catch (...) {
                severity = boost::wave::util::severity_fatal;
            }

            if (severity == boost::wave::util::severity_remark ||
                severity == boost::wave::util::severity_warning) {
                return false;
            }

            std::string error_file;
            try {
                error_file = e.file_name();
            } catch (...) {}

            if (main_input_file.empty() || error_file != main_input_file) {
                return false;
            }

            try {
                dump_error_to_log(ctx, e);
            } catch (...) {
                std::cerr << "⚠️  Error while dumping error log" << std::endl;
            }

            if (!continue_on_error) {
                state->disable_printing = true;
                fatal_error_occurred = true;
            }

            return true;
        }

        template <typename ContextT>
        void start(ContextT& ctx) {
            try {
                auto pos = ctx.get_main_pos();
                main_input_file = std::string(pos.get_file().begin(), pos.get_file().end());
            } catch (...) {}

            if (debug) return;

            try {
                sink->on_start(ctx);
            } catch (...) {}
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            if (debug) return;
            
            if (state->disable_printing && !continue_on_error) {
                std::cerr << "\n⚠️  Preprocessing stopped due to error - output may be incomplete" << std::endl;
                return;
            }

            try {
                sink->on_complete(ctx);
            } catch (...) {}
        }

        server_state<ContainerT>* state;
        client<TokenT, ContainerT>* sink;
        bool debug;
        bool continue_on_error;

        unsigned int conditional_nesting;
        bool evaluating_conditional;
        bool fatal_error_occurred;
        std::string main_input_file;
    };
}

#endif // PPSTEP_SERVER_HPP
