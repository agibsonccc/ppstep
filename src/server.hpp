#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <vector>
#include <string>
#include <type_traits>
#include <cstdlib>

#include "server_fwd.hpp"
#include "client.hpp"

namespace ppstep {
    template <class ContainerT>
    struct server_state {
        server_state() : expanding(), rescanning() {}

        std::vector<ContainerT> expanding;
        std::vector<std::pair<ContainerT, ContainerT>> rescanning;
    };

    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(server_state<ContainerT>& state, client<TokenT, ContainerT>& sink, bool debug = false) : state(&state), sink(&sink), debug(debug), evaluating_conditional(false), fatal_error_occurred(false)  {}

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
            if (evaluating_conditional || fatal_error_occurred) return false;

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
            }

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            if (evaluating_conditional || fatal_error_occurred) return false;
            
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
            }
            
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || fatal_error_occurred) return;

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
            }
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || fatal_error_occurred) return;

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
            }
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            if (fatal_error_occurred) return false;
            
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
            if (fatal_error_occurred) return false;
            
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
            if (fatal_error_occurred) return false;
            
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
            if (fatal_error_occurred) return;

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
        
        // Extract comprehensive error information from Boost.Wave exceptions
        // This hook is called BEFORE the exception is thrown, giving us a chance to suppress it
        template <typename ContextT, typename ExceptionT>
        bool throw_exception(ContextT& ctx, ExceptionT const& e) {
            // Extract all available error information
            std::string error_msg;
            std::string file;
            int line = 0;
            int column = 0;
            unsigned int error_code = 0;
            
            // Get error description
            try {
                error_msg = e.description();
            } catch (...) {
                try {
                    error_msg = e.what();
                } catch (...) {
                    error_msg = "<unknown error>";
                }
            }
            
            // Get error code to determine severity
            try {
                error_code = static_cast<unsigned int>(e.get_errorcode());
            } catch (...) {
                error_code = 0;
            }
            
            // Get file location using Boost 1.89+ APIs
            try {
                file = e.file_name();
            } catch (...) {
                // Fallback to context position
                try {
                    auto pos = ctx.get_main_pos();
                    file = std::string(pos.get_file().begin(), pos.get_file().end());
                } catch (...) {
                    file = "<unknown>";
                }
            }
            
            // Get line number
            try {
                line = e.line_no();
            } catch (...) {
                // Fallback to context position
                try {
                    auto pos = ctx.get_main_pos();
                    line = pos.get_line();
                } catch (...) {
                    line = 0;
                }
            }
            
            // Get column number if available
            try {
                auto pos = ctx.get_main_pos();
                column = pos.get_column();
            } catch (...) {
                column = 0;
            }
            
            // Determine error severity using multiple heuristics
            // Boost.Wave error codes: https://www.boost.org/doc/libs/1_89_0/libs/wave/doc/class_reference_exceptions.html
            // Warning codes typically: 0-99
            // Regular errors: 100-199
            // Fatal/lexical errors: 200+
            
            bool is_warning = false;
            bool is_fatal = false;
            
            // Check if explicitly a warning
            if (error_msg.find("warning:") != std::string::npos ||
                error_msg.find("Warning:") != std::string::npos ||
                error_code < 100) {
                is_warning = true;
            }
            
            // Check for fatal errors that corrupt the lexer state
            // These MUST terminate because the iterator becomes invalid
            if (error_msg.find("Unterminated") != std::string::npos ||
                error_msg.find("unterminated") != std::string::npos ||
                error_msg.find("ill formed") != std::string::npos ||
                error_msg.find("ill-formed") != std::string::npos ||
                error_msg.find("lexical") != std::string::npos ||
                error_code >= 200) {
                is_fatal = true;
            }
            
            // WARNINGS: Print and suppress - context remains valid
            if (is_warning) {
                std::cerr << "⚠️  Warning at " << file << ":" << line;
                if (column > 0) std::cerr << ":" << column;
                std::cerr << std::endl;
                std::cerr << "    " << error_msg << std::endl;
                
                // Return false to suppress the exception and allow preprocessing to continue
                return false;
            }
            
            // FATAL ERRORS: Print full diagnostics and terminate
            if (is_fatal) {
                std::cerr << "\n\n";
                std::cerr << "════════════════════════════════════════════════════════════════════\n";
                std::cerr << "🛑 FATAL PREPROCESSING ERROR\n";
                std::cerr << "════════════════════════════════════════════════════════════════════\n";
                std::cerr << "\n";
                std::cerr << "WHY:  " << error_msg << "\n";
                std::cerr << "\n";
                std::cerr << "WHERE:\n";
                std::cerr << "  File:   " << file << "\n";
                std::cerr << "  Line:   " << line << "\n";
                if (column > 0) {
                    std::cerr << "  Column: " << column << "\n";
                }
                std::cerr << "\n";
                
                // Try to get additional context from the expanding/rescanning stacks
                if (state && !state->expanding.empty()) {
                    std::cerr << "CONTEXT:\n";
                    std::cerr << "  Currently expanding macro:\n";
                    std::cerr << "    ";
                    print_token_container(std::cerr, state->expanding.back());
                    std::cerr << "\n";
                }
                
                if (error_code > 0) {
                    std::cerr << "\nError Code: " << error_code << "\n";
                }
                
                std::cerr << "\n";
                std::cerr << "REASON:\n";
                std::cerr << "  This error corrupts the preprocessor's internal state.\n";
                std::cerr << "  Continuing would cause undefined behavior or crashes.\n";
                std::cerr << "\n";
                std::cerr << "SOLUTION:\n";
                std::cerr << "  Fix the syntax error in the source file and try again.\n";
                std::cerr << "════════════════════════════════════════════════════════════════════\n\n";
                
                fatal_error_occurred = true;
                std::exit(1);
            }
            
            // REGULAR ERRORS: Print diagnostics, try to suppress and continue
            // These errors might not corrupt the context, so we try to continue
            std::cerr << "\n";
            std::cerr << "⚠️  Preprocessing Error at " << file << ":" << line;
            if (column > 0) std::cerr << ":" << column;
            std::cerr << "\n";
            std::cerr << "    " << error_msg << "\n";
            
            if (state && !state->expanding.empty()) {
                std::cerr << "    While expanding: ";
                print_token_container(std::cerr, state->expanding.back());
                std::cerr << "\n";
            }
            
            if (error_code > 0) {
                std::cerr << "    Error code: " << error_code << "\n";
            }
            std::cerr << "\n";
            std::cerr << "    Attempting to continue preprocessing...\n";
            std::cerr << "\n";
            
            // Return false to suppress the exception and try to continue
            // If this causes issues, the main loop's exception handler will catch it
            return false;
        }

        template <typename ContextT>
        void start(ContextT& ctx) {
            if (debug) return;

            sink->on_start(ctx);
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            if (debug) return;

            sink->on_complete(ctx);
        }

        server_state<ContainerT>* state;
        client<TokenT, ContainerT>* sink;
        bool debug;

        unsigned int conditional_nesting;
        bool evaluating_conditional;
        bool fatal_error_occurred;
    };
}

#endif // PPSTEP_SERVER_HPP
