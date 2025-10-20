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
    po::variables_map args;
    if (!parse_args(argc, argv, args))
        return 1;

    char const* input_file = args["input-file"].as<std::string>().c_str();
    auto instring = read_entire_file(std::ifstream(input_file));

    auto server_state = ppstep::server_state<token_sequence_type>();
    auto client = ppstep::client<token_type, token_sequence_type>(server_state);
    auto server = ppstep::server<token_type, token_sequence_type>(server_state, client,  args.count("debug"));
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

    auto first = ctx.begin();
    auto last = ctx.end();
    
    try {
        server.start(ctx);
        
        // Main preprocessing loop - exceptions are handled by throw_exception hook
        while (first != last) {
            try {
                server.lexed_token(ctx, *first);
                ++first;
            } catch (boost::wave::cpp_exception const& e) {
                // Wave exception during token iteration
                // Error already handled by throw_exception hook, just continue if possible
                if (args.count("debug")) {
                    std::cerr << "Caught Wave exception during iteration, attempting to continue..." << std::endl;
                }
                try {
                    ++first;  // Try to skip past the problematic token
                } catch (...) {
                    // Can't even advance iterator, must stop
                    if (args.count("debug")) {
                        std::cerr << "Cannot advance iterator, stopping." << std::endl;
                    }
                    break;
                }
            } catch (std::exception const& e) {
                // Other exception
                if (args.count("debug")) {
                    std::cerr << "Caught exception during iteration: " << e.what() << ", attempting to continue..." << std::endl;
                }
                try {
                    ++first;
                } catch (...) {
                    break;
                }
            } catch (...) {
                // Unknown exception
                if (args.count("debug")) {
                    std::cerr << "Caught unknown exception during iteration, attempting to continue..." << std::endl;
                }
                try {
                    ++first;
                } catch (...) {
                    break;
                }
            }
        }
        
        server.complete(ctx);
    } catch (ppstep::session_terminate const& e) {
        // Session terminated normally by user
        return 0;
    } catch (boost::wave::cpp_exception const& e) {
        // Wave exceptions should be handled by throw_exception hook
        // If we get here, it means the error wasn't suppressed
        std::cerr << "Preprocessing error: " << e.description() << std::endl;
        return 1;
    } catch (std::exception const& e) {
        // Unexpected exception
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
