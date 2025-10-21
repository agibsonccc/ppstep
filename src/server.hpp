#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <vector>
#include <string>
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
        bool disable_printing;
    };

    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(server_state<ContainerT>& state, client<TokenT, ContainerT>& sink, bool debug = false, bool continue_on_error = false)
            : state(&state), sink(&sink), debug(debug), continue_on_error(continue_on_error), evaluating_conditional(false), fatal_error_occurred(false), main_input_file() {}

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

        template <typename ContextT, typename IteratorT>
        bool expanding_function_like_macro(
                ContextT& ctx,
                TokenT const& macrodef, std::vector<TokenT> const& formal_args,
                ContainerT const& definition,
                TokenT const& macrocall, std::vector<ContainerT> const& arguments,
                IteratorT const& seqstart, IteratorT const& seqend) {
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;

            auto sanitized_arguments = std::vector<ContainerT>();
            for (auto const& arg_container : arguments) {
                sanitized_arguments.push_back(sanitize(arg_container));
            }

            auto full_call = ContainerT(seqstart, seqend);
            full_call.push_front(macrocall);
            full_call.push_back(*seqend);
            full_call = sanitize(full_call);
            
            if (!debug) {
                sink->on_expand_function(ctx, macrodef, sanitized_arguments, full_call);
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
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
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
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            if (state->expanding.empty()) {
                std::cerr << "⚠️  Warning: expanded_macro called with empty expanding stack" << std::endl;
                return;
            }

            auto const& initial = *(state->expanding.rbegin());
            
            if (!debug) {
                sink->on_expanded(ctx, sanitize(initial), sanitize(result));
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
            if (evaluating_conditional || (fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            if (state->rescanning.empty()) {
                std::cerr << "⚠️  Warning: rescanned_macro called with empty rescanning stack" << std::endl;
                return;
            }

            auto const& [cause, initial] = *(state->rescanning.rbegin());

            if (!debug) {
                sink->on_rescanned(ctx, sanitize(cause), sanitize(initial), sanitize(result));
            } else {
                std::cout << "R: ";
                print_token_container(std::cout, sanitize(initial)) << " -> ";
                print_token_container(std::cout, sanitize(result)) << std::endl;
            }

            state->rescanning.pop_back();
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return false;
            
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
            
            return false;
        }

        template <typename ContextT>
        void lexed_token(ContextT& ctx, TokenT const& result) {
            if (should_skip_token(result)) return;
            if ((fatal_error_occurred && !continue_on_error) || state->disable_printing) return;

            if (!debug) {
                sink->on_lexed(ctx, result);
            } else {
                std::cout << "L: ";
                print_token(std::cout, result) << std::endl;
            }
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
            
            try { 
                error_msg = e.description(); 
            } catch (...) {
                try { 
                    error_msg = e.what(); 
                } catch (...) {}
            }
            
            try { 
                file = e.file_name(); 
            } catch (...) {}
            
            try { 
                line = e.line_no(); 
            } catch (...) {}
            
            log << "ERROR: " << error_msg << std::endl;
            log << "FILE: " << file << std::endl;
            log << "LINE: " << line << std::endl;
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

            dump_error_to_log(ctx, e);

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

            sink->on_start(ctx);
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            if (debug) return;
            
            if (state->disable_printing && !continue_on_error) {
                std::cerr << "\n⚠️  Preprocessing stopped due to error - output may be incomplete" << std::endl;
                return;
            }

            sink->on_complete(ctx);
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
