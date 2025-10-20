#ifndef PPSTEP_CLIENT_FWD_HPP
#define PPSTEP_CLIENT_FWD_HPP

namespace ppstep {
    enum class preprocessing_event_type {
        CALL,
        EXPANDED,
        RESCANNED,
        LEXED
    };

    enum class stepping_mode {
        FREE,
        UNTIL_BREAK
    };

    struct session_terminate {};
    
    // Exception thrown when preprocessing must stop due to fatal error
    struct preprocessing_fatal_error {
        std::string message;
        preprocessing_fatal_error(const std::string& msg) : message(msg) {}
    };
    
    // Forward declaration of client template
    template <class TokenT, class ContainerT>
    struct client;
}

#endif // PPSTEP_CLIENT_FWD_HPP
