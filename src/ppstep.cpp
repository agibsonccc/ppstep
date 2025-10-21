#define BOOST_WAVE_ENABLE_COMMANDLINE_MACROS 1
#define BOOST_NO_MEMBER_TEMPLATE_FRIENDS 1

#include <string>
#include <iostream>
#include <list>
#include <vector>

#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_token.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <boost/wave/cpplexer/re2clex/cpp_re2c_lexer.hpp>

#include <boost/program_options.hpp>

#include "client.hpp"
#include "server.hpp"
#include "crash_handler.hpp"


namespace po = boost::program_options;

using token_type = boost::wave::cpplexer::lex_token<>;

using token_sequence_type = std::list<token_type, boost::fast_pool_allocator<token_type>>;

using lex_iterator_type = boost::wave::cpplexer::lex_iterator<token_type>;

using context_type =
    boost::wave::context<
        std::string::iterator,
        lex_iterator_type,
        boost::wave::iteration_context_policies::load_file_to_string,
        ppstep::server<token_type, token_sequence_type>
    >;

static std::string read_entire_file(std::istream&& instream) {
    instream.unsetf(std::ios::skipws);

    return std::string(std::istreambuf_iterator<char>(instream.rdbuf()), std::istreambuf_iterator<char>());
}

bool parse_args(int argc, char const** argv, po::variables_map& vm) {
    po::options_description desc("ppstep");
    desc.add_options()
        ("help,h", "produce help message")
        ("include,I", po::value<std::vector<std::string>>()->composing(),
                "include path")
        ("define,D", po::value<std::vector<std::string> >()->composing(),
                "specify a macro to define (as macro[=[value]])")
        ("undefine,U", po::value<std::vector<std::string> >()->composing(),
            "specify a macro to undefine")
        ("debug", "enable debug tracing")
        ("continue-on-error", "continue preprocessing after errors and collect all errors")
        ("input-file", po::value<std::string>()->required(), "input file");

    po::positional_options_description p;
    p.add("input-file", -1);

    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);

    if (vm.count("help")) {
        std::cerr << desc << std::endl;
        return false;
    }

    try {
        po::notify(vm);
        return true;
    } catch(std::exception const& e) {
        std::cerr << "error: " << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return false;
    }
}

int main(int argc, char const** argv) {
    // Install crash handlers FIRST before anything else
    ppstep::install_crash_handlers();
    
    po::variables_map args;
    if (!parse_args(argc, argv, args))
        return 1;

    char const* input_file = args["input-file"].as<std::string>().c_str();
    
    // Set initial context for crash handler
    ppstep::crash_context_guard::set_file(input_file, 0, 0);
    ppstep::crash_context_guard::set_operation("initialization");
    
    auto instring = read_entire_file(std::ifstream(input_file));

    auto server_state = ppstep::server_state<token_sequence_type>();
    auto client = ppstep::client<token_type, token_sequence_type>(server_state);
    bool continue_on_error = args.count("continue-on-error") > 0;
    auto server = ppstep::server<token_type, token_sequence_type>(server_state, client, args.count("debug"), continue_on_error);
    context_type ctx(instring.begin(), instring.end(), input_file, server);

    static_assert(std::is_same_v<token_sequence_type, typename context_type::token_sequence_type>,
                  "wave context token container type not same as expansion tracer token container type");
    
    ctx.set_language(boost::wave::language_support(
        boost::wave::support_cpp2a
        | boost::wave::support_option_va_opt
        | boost::wave::support_option_convert_trigraphs
        | boost::wave::support_option_long_long
        | boost::wave::support_option_include_guard_detection
        | boost::wave::support_option_emit_pragma_directives
        | boost::wave::support_option_insert_whitespace));
    
    if (args.count("include")) {
        for (auto const& path : args["include"].as<std::vector<std::string>>()) {
            ctx.add_include_path(path.c_str());
            ctx.add_sysinclude_path(path.c_str());
        }
    }
    
    if (args.count("define")) {
        for (auto const& definition : args["define"].as<std::vector<std::string>>()) {
            ctx.add_macro_definition(definition);
        }
    }
    
    if (args.count("undefine")) {
        for (auto const& definition : args["undefine"].as<std::vector<std::string>>()) {
            ctx.remove_macro_definition(definition, true);
        }
    }

    int error_count = 0;

    try {
        ppstep::crash_context_guard::set_operation("starting preprocessing");
        server.start(ctx);
        
        ppstep::crash_context_guard::set_operation("token iteration");
        
        auto first = ctx.begin();
        auto last = ctx.end();
        
        while (first != last) {
            // Update crash context with current position
            auto pos = ctx.get_main_pos();
            ppstep::crash_context_guard::set_file(
                pos.get_file().c_str(),
                pos.get_line(),
                pos.get_column()
            );
            
            try {
                const auto& token = *first;
                
                // Update token context (be careful - token might be corrupted)
                try {
                    if (token.is_valid()) {
                        auto val = token.get_value();
                        if (!val.empty()) {
                            // Store token string (limit length for safety)
                            static thread_local char token_buffer[256];
                            size_t len = std::min(val.size(), sizeof(token_buffer) - 1);
                            std::memcpy(token_buffer, val.c_str(), len);
                            token_buffer[len] = '\0';
                            ppstep::crash_context_guard::set_token(token_buffer);
                        }
                    }
                } catch (...) {
                    // Token is corrupted, don't update context
                    ppstep::crash_context_guard::set_token("<corrupted_token>");
                }
                
                server.lexed_token(ctx, token);
                ++first;
                
            } catch (boost::wave::cpp_exception const& e) {
                error_count++;
                
                if (continue_on_error) {
                    // Error already logged by server's throw_exception hook
                    // Continue to next token
                    std::cerr << "\n⚠️  Error #" << error_count << " (continuing due to --continue-on-error)" << std::endl;
                    
                    // Try to advance past the error
                    try {
                        ++first;
                    } catch (...) {
                        std::cerr << "⚠️  Cannot advance past error, stopping iteration" << std::endl;
                        break;
                    }
                } else {
                    // Stop on first error
                    throw;
                }
            }
        }
        
        ppstep::crash_context_guard::set_operation("completing preprocessing");
        server.complete(ctx);
        
    } catch (ppstep::session_terminate const&) {
        // User quit - normal exit
        ppstep::crash_context_guard::clear();
        return 0;
        
    } catch (boost::wave::cpp_exception const& e) {
        // Wave exception - already logged by our hook
        if (!continue_on_error) {
            std::cerr << "\n⚠️  Stopping preprocessing due to error (processed what we could)" << std::endl;
        }
        ppstep::crash_context_guard::clear();
        
        if (error_count > 0) {
            std::cerr << "\n📊 Total errors encountered: " << error_count << std::endl;
            std::cerr << "💾 Check ppstep_error_*.log files for detailed error context" << std::endl;
        }
        return 0;
        
    } catch (std::exception const& e) {
        std::cerr << "\n🔴 Unexpected error: " << e.what() << std::endl;
        ppstep::crash_context_guard::clear();
        return 1;
        
    } catch (...) {
        std::cerr << "\n🔴 Unknown error" << std::endl;
        ppstep::crash_context_guard::clear();
        return 1;
    }

    ppstep::crash_context_guard::clear();
    
    if (error_count > 0) {
        std::cerr << "\n📊 Total errors encountered: " << error_count << std::endl;
        std::cerr << "💾 Check ppstep_error_*.log files for detailed error context" << std::endl;
    } else {
        std::cerr << "\n✅ Preprocessing completed successfully" << std::endl;
    }
    
    return 0;
}
