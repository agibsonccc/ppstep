#ifndef PPSTEP_VIEW_HPP
#define PPSTEP_VIEW_HPP

#include <vector>
#include <string>
#include <variant>
#include <cstdlib>

#include <boost/wave/grammars/cpp_grammar_gen.hpp>

#include <boost/spirit/include/qi.hpp>

#include <boost/filesystem/path.hpp>

#include <linenoise/linenoise.h>

#include "client_fwd.hpp"
#include "server_fwd.hpp"
#include "utils.hpp"


namespace ppstep::detail {
    template <typename ContextT>
    static bool parse_pp_declaration(ContextT &ctx, std::string const& decl) {
        using position_type = typename ContextT::position_type;
        using iterator_type = typename ContextT::iterator_type;
        
        auto begin = iterator_type(ctx, decl.begin(), decl.end(), position_type("<command line>"));
        auto end = iterator_type();
        
        while (begin != end) {
            ++begin;
        }
        
        return true;
    }
}

namespace ppstep {
    using namespace boost::spirit;

    template <class TokenT, class ContainerT>
    struct client_cli {

        client_cli(client<TokenT, ContainerT>& cl, std::string prefix) : cl(cl), steps_requested(0), prefix(std::move(prefix)) {}

        template <class Attr>
        void step(Attr const& attr) {
            if (attr) {
                steps_requested = boost::fusion::at_c<1>(*attr);
            } else {
                steps_requested = 1;
            }
        }

        template <class Attr>
        void add_breakpoint(Attr const& attr, preprocessing_event_type cond) {
            cl.add_breakpoint({attr.begin(), attr.end()}, cond);
        }

        template <class Attr>
        void remove_breakpoint(Attr const& attr, preprocessing_event_type cond) {
            cl.remove_breakpoint({attr.begin(), attr.end()}, cond);
        }

        void step_continue() {
            steps_requested = 1;
            cl.set_mode(stepping_mode::UNTIL_BREAK);
        }
        
        // Recording commands
        template <class Attr>
        void start_record(Attr const& attr) {
            std::string filename(attr.begin(), attr.end());
            if (cl.start_recording(filename)) {
                std::cout << "Recording to " << filename << std::endl;
            } else {
                std::cout << "Failed to open " << filename << " for recording" << std::endl;
            }
        }
        
        void stop_record() {
            cl.stop_recording();
            std::cout << "Recording stopped" << std::endl;
        }
        
        void status() {
            if (cl.is_recording()) {
                std::cout << "Recording to: " << cl.get_record_filename() << std::endl;
            } else {
                std::cout << "Not recording" << std::endl;
            }
        }
        
        template <class ContextT, class Attr>
        void expand_macro(ContextT& ctx, Attr const& attr) {
            using position_type = typename ContextT::position_type;
            using token_sequence_type = typename ContextT::token_sequence_type;
            using lex_iterator_type = typename ContextT::lexer_type;
            
            auto macro = std::string(attr.begin(), attr.end());
            
            auto begin = lex_iterator_type(macro.begin(), macro.end(), position_type("<command line>"), ctx.get_language());
            auto end = lex_iterator_type();
            
            token_sequence_type pending;
            token_sequence_type expanded;
            bool seen_newline;
            
            auto old_hooks = std::move(ctx.get_hooks());

            auto new_state = server_state<ContainerT>();
            auto new_client = client<TokenT, ContainerT>(new_state, macro);
            ctx.get_hooks() = server<TokenT, ContainerT>(new_state, new_client);
            auto token = ctx.expand_tokensequence(begin, end, pending, expanded, seen_newline);

            ctx.get_hooks() = std::move(old_hooks);
        }
        
        template <class Context, class Attr>
        void define_macro(Context& ctx, Attr const& attr) {
            auto decl = std::string("#define ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context, class Attr>
        void undefine_macro(Context& ctx, Attr const& attr) {
            auto decl = std::string("#undef ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context, class Attr>
        void include_file(Context& ctx, Attr const& attr) {
            auto decl = std::string("#include ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context>
        void show_macros(Context const& ctx) {
            for (auto it = ctx.macro_names_begin(); it != ctx.macro_names_end(); ++it) {
                if (it->rfind("__", 0) == 0) continue; // predefined macro

                bool has_params, is_predefined;
                typename Context::position_type pos;
                std::vector<typename Context::token_type> parameters;
                typename Context::token_sequence_type definition;
                ctx.get_macro_definition(*it, has_params, is_predefined, pos, parameters, definition);
                
                std::cout << " - " << *it;
                if (has_params) {
                    std::cout << '(';

                    auto params_it = parameters.begin();
                    auto params_end = parameters.end();

                    if (params_it != params_end)
                        std::cout << (params_it++)->get_value();

                    for (; params_it != params_end; ++params_it)
                        std::cout << ", " << params_it->get_value();

                    std::cout << ')';

                }
                
                std::cout << " ";
                for (auto const& token : definition) {
                    std::cout << token.get_value();
                }
                std::cout << '\n';
            }
            std::cout << std::flush;
        }
        
        void expanding_trace() {
            auto const& expanding = cl.get_state().expanding;

            std::size_t idx = 0;
            for (auto it = expanding.rbegin(); it != expanding.rend(); ++it, ++idx) {
                std::cout << idx << ": ";
                print_token_container(std::cout, *it) << std::endl;
            }
        }
        
        void rescanning_trace() {
            auto const& rescanning = cl.get_state().rescanning;

            std::size_t idx = 0;
            for (auto it = rescanning.rbegin(); it != rescanning.rend(); ++it, ++idx) {
                auto const& [cause, initial] = *it;
                std::cout << idx << ": ";
                print_token_container(std::cout, initial) << '\n';
                
                std::size_t padding_width = idx == 0 ? 1 : 0;
                for (std::size_t i = idx; i != 0; i /= 10) {
                    ++padding_width;
                }
                std::cout << std::string(padding_width, ' ') << "  caused by ";
                print_token_container(std::cout, cause) << std::endl;
            }
        }

        void quit() {
            throw session_terminate();
        }
        
        void explain_current_state() {
            auto latest = cl.newest_history();
            if (latest == cl.oldest_history())
                return;
            
            std::visit([&latest](auto const& event){ event.explain(std::cout); }, latest->event);
        }

        template <class ContextT>
        void current_state(ContextT& ctx) {
            auto latest = cl.newest_history();
            if (latest == cl.oldest_history())
                return;
            
            auto pos = ctx.get_main_pos();
            auto pos_file = boost::filesystem::path(pos.get_file().begin(), pos.get_file().end()).filename().string();
            std::cout << '[' << pos_file << ':' << pos.get_line() << ':'  << pos.get_column() << "]: ";
            std::visit([&latest](auto const& event){ event.print(std::cout, latest->tokens); }, latest->event);
        }

        template <class ContextT, typename Iterator>
        bool parse(ContextT& ctx, Iterator first, Iterator last) {
            using qi::lit;
            using qi::print;
            using qi::uint_;
            using qi::alpha;
            using qi::lexeme;
            using qi::alnum;
            using qi::eoi;
            using qi::eol;
            using qi::phrase_parse;
            using ascii::char_;
            using ascii::space;
            using ascii::space_type;

            auto anything = +(print);

#define PPSTEP_ACTION(...) ([this, &ctx](auto const& attr){ __VA_ARGS__; })

            qi::rule<Iterator, ascii::space_type> grammar =
                lexeme[(lit("step") | lit("s")) >> -(+space >> uint_)][PPSTEP_ACTION(step(attr))]
              | (lit("continue") | lit("c"))[PPSTEP_ACTION(step_continue())]
              | lexeme[(lit("backtrace") | lit("bt"))[PPSTEP_ACTION(expanding_trace())]]
              | lexeme[(lit("forwardtrace") | lit("ft"))[PPSTEP_ACTION(rescanning_trace())]]
              | lexeme[
                  (lit("break") | lit("b")) >> *space > (
                        ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::CALL))])
                      | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                      | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                      | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::LEXED))])
                )]
              | lexeme[
                  (lit("delete") | lit("d")) >> *space > (
                        ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::CALL))])
                      | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                      | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                      | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::LEXED))])
                )]
              | lexeme[(lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(expand_macro(ctx, attr))]]

              | lexeme[lit("#define") > +space > anything[PPSTEP_ACTION(define_macro(ctx, attr))]]
              | lexeme[lit("#undef") > +space > anything[PPSTEP_ACTION(undefine_macro(ctx, attr))]]
              | lexeme[lit("#include") > +space > anything[PPSTEP_ACTION(include_file(ctx, attr))]]
              
              // Recording commands - avoid hyphen issues
              | lexeme[(lit("record") | lit("rec")) > +space > anything[PPSTEP_ACTION(start_record(attr))]]
              | (lit("stoprecord") | lit("sr"))[PPSTEP_ACTION(stop_record())]
              | lit("status")[PPSTEP_ACTION(status())]
              
              | (lit("what") | lit("?"))[PPSTEP_ACTION(explain_current_state())]
              | lit("macros")[PPSTEP_ACTION(show_macros(ctx))]
              | (lit("quit") | lit("q"))[PPSTEP_ACTION(quit())]
              | eoi[PPSTEP_ACTION(current_state(ctx))];

#undef PPSTEP_ACTION

            qi::on_error<qi::fail>(grammar, [](auto const& args, auto const& ctx, auto const&) {
                std::cout << "Found unexpected argument \"" << boost::fusion::at_c<2>(args) << "\" while parsing \"" << boost::fusion::at_c<0>(args) << "\". Expected: " << boost::fusion::at_c<3>(args) << std::endl;
            });

            bool r = phrase_parse(first, last, grammar, space);
            if (first != last) {
                return false;
            }
            return r;
        }

        template <class ContextT>
        void prompt(ContextT& ctx, std::string const& trigger, bool print_state = true) {
            if (steps_requested > 0) --steps_requested;
            if (steps_requested) return;

            cl.set_mode(stepping_mode::FREE);

            if (print_state) current_state(ctx);

            auto prompt = std::string("pp");
            if (!prefix.empty()) {
                prompt += " [" + prefix + ']';
            }
            if (!trigger.empty()) {
                prompt += " (" + trigger + ')';
            }
            prompt += "> ";

            for (char* raw_line; (raw_line = linenoise(prompt.c_str())) != nullptr;) {
                linenoiseHistoryAdd(raw_line);

                bool valid = parse(ctx, raw_line, raw_line + std::strlen(raw_line));
                if (!valid) {
                    std::cout << "Undefined command: \"" << raw_line << "\"." << std::endl;
                }

                std::free(static_cast<void*>(raw_line));

                if (valid) {
                    if (steps_requested) break;
                }
            }
        }

    private:
        client<TokenT, ContainerT>& cl;
        std::size_t steps_requested;
        std::string prefix;
    };
}

#endif // PPSTEP_VIEW_HPP