#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aetheris::js {

// A compact backtracking regular-expression engine covering the constructs
// commonly used on the web: literals, '.', character classes, anchors,
// alternation, groups and the *, +, ?, {n}, {n,} and {n,m} quantifiers, with
// the 'g' (global) and 'i' (ignore-case) flags. It is deliberately small rather
// than a fully ECMAScript-compliant engine: word-boundary assertions, capture
// groups, back-references, look-around and named groups are not supported.
class Regex {
public:
    static std::shared_ptr<Regex> compile(std::string const& pattern, std::string const& flags, std::string& error);

    // Finds the first match at or after `start`, reporting its extent.
    bool search(std::string const& text, std::size_t start, std::size_t& match_start, std::size_t& match_end,
        std::vector<std::pair<std::size_t, std::size_t>>* captures = nullptr) const;
    // Number of capturing groups in the pattern.
    std::size_t group_count() const;
    bool test(std::string const& text) const;

    std::string const& source() const;
    std::string const& flags() const;
    bool global() const;
    bool ignore_case() const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace aetheris::js
