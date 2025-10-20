#ifndef PPSTEP_UTILS_HPP
#define PPSTEP_UTILS_HPP

#include <algorithm>
#include <utility>
#include <vector>
#include <optional>

namespace ppstep {

    template <class Iterator, class Printer, class Delimiter>
    std::ostream& print_with_delimiter(std::ostream& os, Iterator& it, Iterator end, Printer printer, Delimiter const& delimiter) {
        if (it == end) return os;

        printer(os, *it++);
        for (; it != end; ++it) {
            os << delimiter;
            printer(os, *it);
        }
        return os;
    }

    template <class Token>
    std::ostream& print_token(std::ostream& os, Token const& token) {
        // Defensive: check if token is valid before accessing its value
        if (!token.is_valid()) {
            os << "<invalid_token>";
            return os;
        }
        
        try {
            // Try to get the value - may fail if token is corrupted
            auto value = token.get_value();
            
            // Check if we can safely access c_str()
            // The string object might be corrupt but the pointer might still be readable
            try {
                const char* str = value.c_str();
                if (str != nullptr) {
                    os << str;
                } else {
                    os << "<null_string>";
                }
            } catch (...) {
                os << "<corrupted_string>";
            }
        } catch (...) {
            // If get_value() itself throws or accesses bad memory
            os << "<error_getting_value>";
        }
        
        return os;
    }
    
    template <class Iterator>
    std::ostream& print_token_range(std::ostream& os, Iterator& it, Iterator end) {
        return print_with_delimiter(os, it, std::move(end), [](auto& os, auto const& token) { print_token(os, token); }, ' ');
    }

    template <class Container>
    std::ostream& print_token_container(std::ostream& os, Container const& data) {
        auto it = std::begin(data);
        return print_token_range(os, it, std::end(data));
    }
    
    // New function to print tokens with preserved whitespace (no artificial delimiters)
    template <class Container>
    std::ostream& print_token_container_preserved(std::ostream& os, Container const& data) {
        for (auto const& token : data) {
            // Defensive: handle corrupted tokens
            if (!token.is_valid()) {
                os << "<invalid_token>";
                continue;
            }
            
            try {
                os << token.get_value();
            } catch (...) {
                os << "<error>";
            }
        }
        return os;
    }
    
    // Helper to reconstruct text with intelligent spacing
    template <class Container>
    std::string reconstruct_with_spacing(Container const& tokens) {
        std::stringstream ss;
        bool need_space = false;
        
        for (auto const& tok : tokens) {
            // Defensive: skip invalid tokens
            if (!tok.is_valid()) {
                ss << "<invalid>";
                need_space = true;
                continue;
            }
            
            std::string val;
            try {
                val = std::string(tok.get_value().c_str());
            } catch (...) {
                ss << "<error>";
                need_space = true;
                continue;
            }
            
            // Check if we need to add spacing
            if (need_space && !val.empty()) {
                // Don't add space before certain punctuation
                char first_char = val[0];
                if (first_char != ',' && first_char != ';' && first_char != ')' && 
                    first_char != ']' && first_char != '}' && first_char != '.' &&
                    first_char != '-' && first_char != '+' && first_char != '*' && 
                    first_char != '/' && first_char != '%' && first_char != '=' &&
                    first_char != '<' && first_char != '>' && first_char != '!' &&
                    first_char != '&' && first_char != '|' && first_char != '^' &&
                    first_char != '~' && first_char != '?' && first_char != ':') {
                    ss << " ";
                }
            }
            
            ss << val;
            
            // Set flag for next iteration
            if (!val.empty()) {
                char last_char = val[val.size() - 1];
                need_space = (last_char != '(' && last_char != '[' && last_char != '{' &&
                             last_char != ' ' && last_char != '\t' && last_char != '\n' &&
                             last_char != '\r');
            }
        }
        
        return ss.str();
    }

    template <class Container, class T>
    auto join_lists(Container const& lists, T const& separator) {
        auto acc = std::vector<T>();

        auto it = std::begin(lists);
        {
            auto const& list = *it++;
            acc.insert(std::end(acc), std::begin(list), std::end(list));
        }

        auto end = std::end(lists);
        for (; it != end; ++it) {
            acc.push_back(separator);
            acc.insert(std::end(acc), std::begin(*it), std::end(*it));
        }

        return acc;
    }

    template <class Container>
    std::optional<std::pair<typename Container::const_iterator, typename Container::const_iterator>>
    find_sublist(Container const& data, Container const& pattern, typename Container::const_iterator it) {
        auto end = std::end(data);
        auto match = std::search(it, end, std::begin(pattern), std::end(pattern));
        if (match != end) {
            return {{match, std::next(match, pattern.size())}};
        } else {
            return {};
        }
    }
}

#endif // PPSTEP_UTILS_HPP
