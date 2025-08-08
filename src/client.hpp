#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <optional>
#include <variant>
#include <tuple>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>
#include <chrono>
#include <ctime>
#include <iomanip>

#include "server_fwd.hpp"
#include "client_fwd.hpp"
#include "view.hpp"
#include "utils.hpp"

namespace ppstep {
    namespace ansi {
        constexpr auto black_fg = "\u001b[30m";
        constexpr auto white_fg = "\u001b[37;1m";

        constexpr auto yellow_bg = "\u001b[43m";
        constexpr auto blue_bg = "\u001b[44;1m";
        constexpr auto white_bg = "\u001b[47m";

        constexpr auto bold = "\u001b[1m";

        constexpr auto reset = "\u001b[0m";
    }

    namespace events {
        template <class ContainerT, class DerivedT>
        struct formatting_event {
            formatting_event(std::size_t start, std::size_t end) : start(start), end(end) {}

            void print(std::ostream& os, ContainerT const& tokens) const {
                auto sub_start = std::next(tokens.begin(), start);
                auto sub_end = std::next(tokens.begin(), end);

                auto it = tokens.begin();
                auto end = tokens.end();
                
                os << ansi::bold;
                print_token_range(os, it, sub_start);
                if (it != tokens.begin())
                    os << ' ';

                static_cast<DerivedT const*>(this)->format(os);
                if (sub_start != sub_end) {
                    print_token_range(os, sub_start, sub_end) << ansi::reset;
                } else {
                    os << ' ' << ansi::reset;
                }
                os << ansi::bold;
                if (sub_end != end)
                    os << ' ';

                print_token_range(os, sub_end, tokens.end()) << ansi::reset << std::endl;
            }
            
            std::size_t start, end;
        };

        template <class ContainerT>
        struct call : formatting_event<ContainerT, call<ContainerT>> {
            call(ContainerT tokens, std::size_t start, std::size_t end)
                : formatting_event<ContainerT, call<ContainerT>>(start, end), tokens(std::move(tokens)) {}

            void format(std::ostream& os) const {
                os << ansi::white_bg << ansi::black_fg;
            }
            
            void explain(std::ostream& os) const {
                os << "called macro " << ansi::white_bg << ansi::black_fg;
                print_token_container(os, tokens) << ansi::reset << std::endl;
            }

            ContainerT tokens;
        };
        
        template <class ContainerT>
        struct expanded : formatting_event<ContainerT, expanded<ContainerT>> {
            expanded(ContainerT initial, std::size_t start, std::size_t end)
                : formatting_event<ContainerT, expanded<ContainerT>>(start, end), initial(std::move(initial)) {}

            void format(std::ostream& os) const {
                os << ansi::yellow_bg << ansi::black_fg;
            }
            
            void explain(std::ostream& os) const {
                os << "expanded macro " << ansi::white_bg << ansi::black_fg;
                print_token_container(os, initial) << ansi::reset << std::endl;
            }
            
            ContainerT initial;
        };
        
        template <class ContainerT>
        struct rescanned : formatting_event<ContainerT, rescanned<ContainerT>> {
            rescanned(ContainerT cause, ContainerT initial, std::size_t start, std::size_t end)
                : formatting_event<ContainerT, rescanned<ContainerT>>(start, end), cause(std::move(cause)), initial(std::move(initial)) {}

            void format(std::ostream& os) const {
                os << ansi::blue_bg << ansi::white_fg;
            }
            
            void explain(std::ostream& os) const {
                os << "rescanned macro " << ansi::yellow_bg << ansi::black_fg;
                print_token_container(os, initial) << ansi::reset << "\ncaused by " << ansi::white_bg << ansi::black_fg;
                print_token_container(os, cause) << ansi::reset << std::endl;
            }
            
            ContainerT cause, initial;
        };
        
        template <class ContainerT>
        struct lexed {
            void print(std::ostream& os, ContainerT const& tokens) const {
                os << ansi::bold;
                print_token_container(os, tokens) << ansi::reset << std::endl;
            }
            
            void explain(std::ostream& os) const {
                os << "lexed tokens ?" << std::endl;
            }
        };
    }
    
    template <class ContainerT>
    using preprocessing_event =
        std::variant<
            events::call<ContainerT>,
            events::expanded<ContainerT>,
            events::rescanned<ContainerT>,
            events::lexed<ContainerT>>;
    
    template <class ContainerT>
    struct offset_container {
        using iterator = typename ContainerT::const_iterator;
        
        offset_container(ContainerT&& tokens, iterator&& start) : tokens(std::move(tokens)), start(std::move(start)) {}
        
        offset_container(ContainerT&& tokens) : tokens(std::move(tokens)), start(this->tokens.end()) {}
        
        offset_container(offset_container<ContainerT> const&) = delete;
        
        std::optional<std::pair<iterator, iterator>> find_pattern(ContainerT const& pattern) const {
            return find_sublist(tokens, pattern, start);
        }
        
        ContainerT tokens;
        iterator start;
    };
    
    template <class ContainerT>
    struct historical_event {
        historical_event(ContainerT tokens, preprocessing_event<ContainerT>&& event) : tokens(std::move(tokens)), event(std::move(event)) {}

        ContainerT tokens;
        preprocessing_event<ContainerT> event;
    };
    
    template <class TokenT, class ContainerT>
    struct client {
        client(server_state<ContainerT>& state, std::string prefix) : state(&state), cli(client_cli<TokenT, ContainerT>(*this, std::move(prefix))), mode(stepping_mode::FREE), recording_active(false) {}
        
        client(server_state<ContainerT>& state) : client(state, "") {}

        // Recording functionality
        bool start_recording(const std::string& filename) {
            if (recording_active) {
                stop_recording();
            }
            
            record_file.open(filename, std::ios::out | std::ios::trunc);
            if (!record_file.is_open()) {
                return false;
            }
            
            recording_active = true;
            record_filename = filename;
            
            // Write header with timestamp
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            record_file << "=== PPSTEP TRACE ===" << std::endl;
            record_file << "Started: " << std::ctime(&time_t);
            record_file << "===================" << std::endl << std::endl;
            
            return true;
        }
        
        void stop_recording() {
            if (recording_active) {
                record_file << std::endl << "=== END OF TRACE ===" << std::endl;
                record_file.close();
                recording_active = false;
                record_filename.clear();
            }
        }
        
        bool is_recording() const {
            return recording_active;
        }
        
        std::string get_record_filename() const {
            return record_filename;
        }
        
        // Helper function to output tokens with normalized whitespace
        void output_tokens_normalized(std::ostream& os, const ContainerT& tokens) {
            bool prev_was_whitespace = false;
            bool need_space = false;
            
            for (const auto& tok : tokens) {
                // Check if this is a whitespace token
                if (IS_CATEGORY(tok, boost::wave::WhiteSpaceTokenType)) {
                    // Mark that we've seen whitespace but don't output it yet
                    if (!prev_was_whitespace && need_space) {
                        // We need whitespace here
                        prev_was_whitespace = true;
                    }
                } else {
                    // This is a non-whitespace token
                    auto val = tok.get_value();
                    
                    // Add a single space if we had whitespace before this token
                    if (prev_was_whitespace || need_space) {
                        // Don't add space at the beginning or before certain punctuation
                        if (!val.empty() && need_space) {
                            char first_char = val.c_str()[0];
                            if (first_char != ',' && first_char != ';' && first_char != ')' && 
                                first_char != ']' && first_char != '}') {
                                os << " ";
                            }
                        }
                    }
                    
                    os << val;
                    
                    // Determine if we need space after this token
                    if (!val.empty()) {
                        char last_char = val.c_str()[val.size() - 1];
                        need_space = (last_char != '(' && last_char != '[' && last_char != '{');
                    } else {
                        need_space = false;
                    }
                    
                    prev_was_whitespace = false;
                }
            }
        }

        template <class ContextT>
        void on_lexed(ContextT& ctx, TokenT const& token) {
            if (token_stack.empty()) {
                auto last_tokens = token_history.empty() ? ContainerT() : newest_history()->tokens;
                last_tokens.push_back(token);

                lexed_tokens.push_back(token);
                token_history.push_back(historical_event<ContainerT>(last_tokens, events::lexed<ContainerT>()));

                // Record lexed token if recording
                if (recording_active) {
                    record_file << "[LEXED] " << token.get_value() << std::endl;
                }

                handle_prompt(ctx, token, preprocessing_event_type::LEXED);

            } else {
                auto const& last_tokens = newest_history()->tokens;

                lex_buffer.push_back(token);
                if (std::equal(std::next(std::begin(last_tokens), lexed_tokens.size()), std::end(last_tokens),
                               std::begin(lex_buffer), std::end(lex_buffer),
                               [](auto const& a, auto const& b) { return a.get_value() == b.get_value(); })) {
                    lexed_tokens.insert(std::end(lexed_tokens), std::begin(lex_buffer), std::end(lex_buffer));
                    lex_buffer.clear();
                    reset_token_stack();
                }
            }
        }

        // Overloaded version with preserved tokens for recording
        template <class ContextT>
        void on_expand_function(ContextT& ctx, TokenT const& call, std::vector<ContainerT> const& arguments, 
                               ContainerT call_tokens, std::vector<ContainerT> const& preserved_arguments, 
                               ContainerT preserved_call_tokens) {
            // Record function-like macro call if recording with normalized whitespace
            if (recording_active) {
                record_file << "[CALL] ";
                output_tokens_normalized(record_file, preserved_call_tokens);
                record_file << std::endl;
                
                // Record arguments with normalized whitespace
                if (!preserved_arguments.empty()) {
                    for (size_t i = 0; i < preserved_arguments.size(); ++i) {
                        record_file << "  ARG[" << i << "]: ";
                        output_tokens_normalized(record_file, preserved_arguments[i]);
                        record_file << std::endl;
                    }
                }
            }

            // Continue with normal processing using sanitized tokens
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(call_tokens, lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }
            
            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }
        
        // Keep original version for backward compatibility
        template <class ContextT>
        void on_expand_function(ContextT& ctx, TokenT const& call, std::vector<ContainerT> const& arguments, ContainerT call_tokens) {
            // Fallback for when preserved versions aren't available
            if (recording_active) {
                record_file << "[CALL] ";
                for (const auto& tok : call_tokens) {
                    record_file << tok.get_value();
                    record_file << " ";
                }
                record_file << std::endl;
                
                if (!arguments.empty()) {
                    for (size_t i = 0; i < arguments.size(); ++i) {
                        record_file << "  ARG[" << i << "]: ";
                        for (const auto& tok : arguments[i]) {
                            record_file << tok.get_value();
                            record_file << " ";
                        }
                        record_file << std::endl;
                    }
                }
            }

            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(call_tokens, lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }
            
            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        template <class ContextT>
        void on_expand_object(ContextT& ctx, TokenT const& call) {
            auto call_tokens = ContainerT{call};
            
            // Record object-like macro call if recording
            if (recording_active) {
                record_file << "[CALL] " << call.get_value() << std::endl;
            }
            
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(call_tokens, lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }

            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        // Overloaded version with preserved tokens
        template <class ContextT>
        void on_expanded(ContextT& ctx, ContainerT const& initial, ContainerT const& result,
                        ContainerT const& preserved_initial, ContainerT const& preserved_result) {
            // Record expansion with normalized whitespace
            if (recording_active) {
                record_file << "[EXPANDED]" << std::endl;
                record_file << "  FROM: ";
                output_tokens_normalized(record_file, preserved_initial);
                record_file << std::endl;
                record_file << "  TO:   ";
                output_tokens_normalized(record_file, preserved_result);
                record_file << std::endl;
            }

            // Continue with normal processing using sanitized tokens
            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);

                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::expanded<ContainerT>(initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::expanded<ContainerT>(initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::EXPANDED);
        }
        
        // Keep original version for backward compatibility
        template <class ContextT>
        void on_expanded(ContextT& ctx, ContainerT const& initial, ContainerT const& result) {
            // Fallback for when preserved versions aren't available
            if (recording_active) {
                record_file << "[EXPANDED]" << std::endl;
                record_file << "  FROM: ";
                for (const auto& tok : initial) {
                    record_file << tok.get_value() << " ";
                }
                record_file << std::endl;
                record_file << "  TO:   ";
                for (const auto& tok : result) {
                    record_file << tok.get_value() << " ";
                }
                record_file << std::endl;
            }

            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);

                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::expanded<ContainerT>(initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::expanded<ContainerT>(initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::EXPANDED);
        }

        // Overloaded version with preserved tokens
        template <class ContextT>
        void on_rescanned(ContextT& ctx, ContainerT const& cause, ContainerT const& initial, ContainerT const& result,
                         ContainerT const& preserved_cause, ContainerT const& preserved_initial, ContainerT const& preserved_result) {
            if (initial.empty()) return;

            // Record rescan with normalized whitespace
            if (recording_active) {
                record_file << "[RESCANNED]" << std::endl;
                record_file << "  FROM:      ";
                output_tokens_normalized(record_file, preserved_initial);
                record_file << std::endl;
                record_file << "  TO:        ";
                output_tokens_normalized(record_file, preserved_result);
                record_file << std::endl;
                record_file << "  CAUSED BY: ";
                output_tokens_normalized(record_file, preserved_cause);
                record_file << std::endl;
            }

            // Continue with normal processing using sanitized tokens
            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);
                
                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::RESCANNED);
        }
        
        // Keep original version for backward compatibility
        template <class ContextT>
        void on_rescanned(ContextT& ctx, ContainerT const& cause, ContainerT const& initial, ContainerT const& result) {
            if (initial.empty()) return;

            // Fallback for when preserved versions aren't available
            if (recording_active) {
                record_file << "[RESCANNED]" << std::endl;
                record_file << "  FROM:      ";
                for (const auto& tok : initial) {
                    record_file << tok.get_value() << " ";
                }
                record_file << std::endl;
                record_file << "  TO:        ";
                for (const auto& tok : result) {
                    record_file << tok.get_value() << " ";
                }
                record_file << std::endl;
                record_file << "  CAUSED BY: ";
                for (const auto& tok : cause) {
                    record_file << tok.get_value() << " ";
                }
                record_file << std::endl;
            }

            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);
                
                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::RESCANNED);
        }
        
        template <typename ContextT, typename ExceptionT>
        void on_exception(ContextT& ctx, ExceptionT const& e) {
            std::cout << e.what() << ": " << e.description() << std::endl;
            cli.prompt(ctx, "exception");
        }

        template <class ContextT>
        void on_complete(ContextT& ctx) {
            cli.prompt(ctx, "complete");
        }
        
        template <class ContextT>
        void on_start(ContextT& ctx) {
            std::cout << "Preprocessing " << ctx.get_main_pos() << '.' << std::endl;
            cli.prompt(ctx, "started", false);
        }

        void add_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    expansion_breakpoints.insert(macro);
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
                    expanded_breakpoints.insert(macro);
                    break;
                }
            }
        }

        void remove_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    expansion_breakpoints.erase(macro);
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
                    expanded_breakpoints.erase(macro);
                    break;
                }
            }
        }
        
        server_state<ContainerT> const& get_state() {
            return *state;
        }

        void set_mode(stepping_mode m) {
            mode = m;
        }

        auto newest_history() {
            return token_history.rbegin();
        }
        
        auto oldest_history() {
            return token_history.rend();
        }

    private:
        using container_iterator = typename ContainerT::const_iterator;
        
        using range_container = std::tuple<ContainerT const*, container_iterator, container_iterator>;

        ContainerT prepend_lexed(ContainerT const& tokens) {
            auto acc = ContainerT(std::begin(lexed_tokens), std::end(lexed_tokens));
            acc.insert(std::end(acc), std::begin(tokens), std::end(tokens));
            return acc;
        }

        void push(ContainerT&& tokens, preprocessing_event<ContainerT>&& event) {
            push(std::move(tokens), std::begin(tokens), std::move(event));
        }

        void push(ContainerT&& tokens, container_iterator&& head, preprocessing_event<ContainerT>&& event) {
            auto historical_tokens = prepend_lexed(tokens);
            token_history.push_back(historical_event<ContainerT>(historical_tokens, std::move(event)));

            if (head != tokens.end()) {
                token_stack.emplace_back(std::move(tokens), std::move(head));
            } else {
                token_stack.emplace_back(std::move(tokens));
            }
        }

        range_container match(ContainerT const& pattern) {
            while (!token_stack.empty()) {
                auto const& top = token_stack.back();

                auto sublist = top.find_pattern(pattern);

                if (sublist) {
                    auto [start, end] = *sublist;

                    return std::make_tuple(&(top.tokens), start, end);
                } else {
                    token_stack.pop_back();
                }
            }
            
            std::stringstream ss;
            print_token_container(ss, pattern);
            throw std::logic_error("could not find pattern \"" + ss.str() + "\" in token stack");
        }
        
        std::optional<std::pair<std::size_t, std::size_t>> find_match_indices(offset_container<ContainerT> const& oc, ContainerT const& pattern) {
            auto sublist = oc.find_pattern(pattern);
            if (sublist) {
                auto [start, end] = *sublist;

                auto begin_to_start = std::distance(oc.tokens.begin(), start);
                auto begin_to_end = begin_to_start + std::distance(start, end);
                return {{begin_to_start, begin_to_end}};
            } else {
                return {};
            }
        }

        void splice_between(ContainerT const& tokens, ContainerT const& result, container_iterator start, container_iterator end,
                                                       ContainerT& new_tokens, std::size_t& new_start, std::size_t& new_end) {
            new_tokens.insert(new_tokens.end(), tokens.begin(), start);
            new_start = new_tokens.size();

            new_tokens.insert(new_tokens.end(), result.begin(), result.end());
            new_end = new_tokens.size();

            new_tokens.insert(new_tokens.end(), end, tokens.end());
        }

        void reset_token_stack() {
            token_stack.clear();
        }
        
        char const* get_preprocessing_event_type_name(preprocessing_event_type type) {
            switch (type) {
                case preprocessing_event_type::CALL: return "called";
                case preprocessing_event_type::EXPANDED: return "expanded";
                case preprocessing_event_type::RESCANNED: return "rescanned";
                case preprocessing_event_type::LEXED: return "lexed";
                default: return "";
            }
        }

        template <class ContextT>
        void handle_prompt(ContextT& ctx, TokenT const& token, preprocessing_event_type type) {
            bool do_prompt = false;

            switch (mode) {
                case stepping_mode::FREE: {
                    do_prompt = true;
                    break;
                }
                case stepping_mode::UNTIL_BREAK: {
                    switch (type) {
                        case preprocessing_event_type::CALL: {
                            if (expansion_breakpoints.find(token.get_value()) != expansion_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case preprocessing_event_type::EXPANDED: {
                            if (expanded_breakpoints.find(token.get_value()) != expanded_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                    }
                    break;
                }
            }

            if (do_prompt) {
                cli.prompt(ctx, get_preprocessing_event_type_name(type));
            }
        }

        server_state<ContainerT>* state;
        client_cli<TokenT, ContainerT> cli;
        std::set<typename TokenT::string_type> expansion_breakpoints;
        std::set<typename TokenT::string_type> expanded_breakpoints;
        stepping_mode mode;

        std::list<offset_container<ContainerT>> token_stack;
        std::vector<historical_event<ContainerT>> token_history;
        std::vector<TokenT> lexed_tokens;
        std::vector<TokenT> lex_buffer;
        
        // Recording state
        std::ofstream record_file;
        bool recording_active;
        std::string record_filename;
    };
}

#endif // PPSTEP_CLIENT_HPP
