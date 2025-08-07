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
        ("skip-gcc-compat", "skip GCC compatibility macros (use if having issues with system headers)")
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
    
    // Enhanced language support with additional options to handle system headers better
    ctx.set_language(boost::wave::language_support(
        boost::wave::support_cpp2a
        | boost::wave::support_option_va_opt
        | boost::wave::support_option_convert_trigraphs
        | boost::wave::support_option_long_long
        | boost::wave::support_option_include_guard_detection
        | boost::wave::support_option_emit_pragma_directives
        | boost::wave::support_option_insert_whitespace
        | boost::wave::support_option_preserve_comments
        | boost::wave::support_option_no_newline_at_end_of_file));
    
    // Set a high include nesting depth to handle complex system headers
    ctx.set_max_include_nesting_depth(1024);
    
    // Add GCC compatibility macros unless explicitly disabled
    // Note: We cannot redefine certain predefined macros like __cplusplus, __STDC__, etc.
    // Boost.Wave manages these internally based on the language settings
    if (!args.count("skip-gcc-compat")) {
        // GCC version macros - these are usually safe to define
        ctx.add_macro_definition("__GNUC__=11");
        ctx.add_macro_definition("__GNUC_MINOR__=0");
        ctx.add_macro_definition("__GNUC_PATCHLEVEL__=0");
        ctx.add_macro_definition("__GXX_ABI_VERSION=1016");
        
        // DON'T define __cplusplus - Boost.Wave manages this internally
        // ctx.add_macro_definition("__cplusplus=202002L"); // REMOVED
        
        // GCC built-in feature test macros (common ones that cause issues)
        // These are the most important ones to fix the "0(0)" error
        ctx.add_macro_definition("__has_builtin(x)=0");
        ctx.add_macro_definition("__has_include(x)=0");
        ctx.add_macro_definition("__has_include_next(x)=0");
        ctx.add_macro_definition("__has_attribute(x)=0");
        ctx.add_macro_definition("__has_cpp_attribute(x)=0");
        ctx.add_macro_definition("__has_feature(x)=0");
        ctx.add_macro_definition("__has_extension(x)=0");
        ctx.add_macro_definition("__has_warning(x)=0");
        
        // Common GCC predefined macros
        ctx.add_macro_definition("__GNUG__=11");
        ctx.add_macro_definition("__GCC_HAVE_DWARF2_CFI_ASM=1");
        ctx.add_macro_definition("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1=1");
        ctx.add_macro_definition("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2=1");
        ctx.add_macro_definition("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4=1");
        ctx.add_macro_definition("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8=1");
        
        // Architecture-specific macros (x86_64 assumed, adjust as needed)
        ctx.add_macro_definition("__x86_64__=1");
        ctx.add_macro_definition("__x86_64=1");
        ctx.add_macro_definition("__amd64__=1");
        ctx.add_macro_definition("__amd64=1");
        ctx.add_macro_definition("__LP64__=1");
        ctx.add_macro_definition("_LP64=1");
        
        // DON'T define these - Boost.Wave manages them:
        // __STDC__, __STDC_VERSION__, __STDC_HOSTED__ - REMOVED
        
        // These are safe to define
        ctx.add_macro_definition("__STDCPP_DEFAULT_NEW_ALIGNMENT__=16");
        ctx.add_macro_definition("__STDCPP_THREADS__=1");
        
        // Linux/Unix macros (adjust based on your system)
        ctx.add_macro_definition("__linux__=1");
        ctx.add_macro_definition("__unix__=1");
        ctx.add_macro_definition("__unix=1");
        ctx.add_macro_definition("__gnu_linux__=1");
        
        if (args.count("debug")) {
            std::cerr << "Added GCC compatibility macros" << std::endl;
        }
    }
    
    if (args.count("include")) {
        for (auto const& path : args["include"].as<std::vector<std::string>>()) {
            ctx.add_include_path(path.c_str());
            // Only add as system include if it's not a system directory
            // to avoid processing system headers unnecessarily
            if (path.find("/usr/include") == std::string::npos &&
                path.find("/usr/local/include") == std::string::npos &&
                path.find("/System/Library") == std::string::npos) {
                ctx.add_sysinclude_path(path.c_str());
            }
        }
    }
    
    if (args.count("define")) {
        for (auto const& definition : args["define"].as<std::vector<std::string>>()) {
            // Skip if trying to redefine protected macros
            std::string def = definition;
            if (def.find("__cplusplus") == 0 || 
                def.find("__STDC__") == 0 ||
                def.find("__STDC_VERSION__") == 0 ||
                def.find("__STDC_HOSTED__") == 0) {
                if (args.count("debug")) {
                    std::cerr << "Warning: Skipping redefinition of protected macro: " << definition << std::endl;
                }
                continue;
            }
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
        while (first != last) {
            server.lexed_token(ctx, *first);
            ++first;
        }
        server.complete(ctx);
    } catch (ppstep::session_terminate const& e) {
        ;
    } catch (boost::wave::macro_handling_exception const& e) {
        // Handle macro-related exceptions
        std::cerr << "Macro handling error: " << e.what() << std::endl;
        std::cerr << "Description: " << e.description() << std::endl;
        
        std::string desc = e.description();
        if (desc.find("may not be redefined") != std::string::npos) {
            std::cerr << "\nNote: Some predefined macros like __cplusplus, __STDC__, etc. cannot be redefined." << std::endl;
            std::cerr << "These are managed internally by Boost.Wave based on language settings." << std::endl;
        }
        return 1;
    } catch (boost::wave::preprocess_exception const& e) {
        // Handle preprocessing exceptions more gracefully
        std::cerr << "Preprocessing error: " << e.what() << std::endl;
        std::cerr << "Description: " << e.description() << std::endl;
        std::cerr << "File: " << e.file_name() << ":" << e.line_no() << ":" << e.column_no() << std::endl;
        
        // Check if it's the specific error we're trying to work around
        std::string desc = e.description();
        if (desc.find("ill formed preprocessor expression") != std::string::npos &&
            desc.find("0(0)") != std::string::npos) {
            std::cerr << "\nThis appears to be a known issue with GCC system headers." << std::endl;
            std::cerr << "Try one of these workarounds:" << std::endl;
            std::cerr << "1. Use --skip-gcc-compat flag" << std::endl;
            std::cerr << "2. Preprocess with gcc first: gcc -E -P " << input_file << " | ppstep -" << std::endl;
            std::cerr << "3. Add -D__has_builtin(x)=0 -D__has_include(x)=0" << std::endl;
        }
        return 1;
    } catch (boost::wave::cpp_exception const& e) {
        std::cerr << "CPP exception: " << e.what() << ": " << e.description() << std::endl;
        return 1;
    } catch (boost::wave::cpplexer::lexing_exception const& e) {
        std::cerr << "Lexing exception: " << e.what() << ": " << e.description() << std::endl;
        return 1;
    } catch (std::exception const& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
