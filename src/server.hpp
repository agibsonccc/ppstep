#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <vector>
#include <string>
#include <type_traits>

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

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            if (evaluating_conditional || fatal_error_occurred) return false;
            
            if (!debug) {
                sink->on_expand_object(ctx, macrocall);
            } else {
                std::cout << "O: ";
                print_token(std::cout, macrocall) << std::endl;
            }

            state->expanding.push_back({macrocall});
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || fatal_error_occurred) return;

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
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional || fatal_error_occurred) return;

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
            if (should_skip_token(result) || fatal_error_occurred) return;

            if (!debug) {
                sink->on_lexed(ctx, result);
            } else {
                std::cout << "L: ";
                print_token(std::cout, result) << std::endl;
            }
        }
        
        // Extract error information from Boost.Wave 1.89+ exceptions
        template <typename ContextT, typename ExceptionT>
        bool throw_exception(ContextT& ctx, ExceptionT const& e) {
            std::string error_msg;
            std::string file;
            int line = 0;
            
            // Get description
            try {
                error_msg = e.description();
            } catch (...) {
                error_msg = e.what();
            }
            
            // Check for truly fatal errors (not warnings or recoverable errors)
            bool is_fatal = false;
            std::string error_lower = error_msg;
            std::transform(error_lower.begin(), error_lower.end(), error_lower.begin(), ::tolower);
            
            // Only mark catastrophic failures as fatal
            // Lexer warnings and syntax errors are NOT fatal
            if (error_lower.find("internal error") != std::string::npos ||
                error_lower.find("unable to open") != std::string::npos ||
                error_lower.find("file not found") != std::string::npos) {
                is_fatal = true;
            }
            
            // Boost 1.89+ has file_name() and line_no() directly on exceptions
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
            
            // Notify client about the error
            if (!debug && sink) {
                sink->on_error(error_msg, file, line);
            } else if (debug) {
                std::cerr << "ERROR: " << error_msg << " at " << file << ":" << line << std::endl;
            }
            
            // Mark fatal errors to stop further processing hooks
            if (is_fatal) {
                fatal_error_occurred = true;
                if (!debug) {
                    std::cerr << "⚠️  Fatal preprocessing error - stopping macro expansion" << std::endl;
                }
            }
            
            // Return false to suppress the exception and continue lexing
            // but hooks will check fatal_error_occurred flag
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
