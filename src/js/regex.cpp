#include "regex.hpp"

#include <cctype>
#include <functional>
#include <utility>
#include <vector>

namespace aetheris::js {

namespace {

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
    enum class Kind { Literal, Any, Class, Start, End, Concat, Alt, Repeat, Group, BackReference, Lookahead, Lookbehind } kind { Kind::Literal };
    char literal { 0 };
    bool negated { false };
    std::vector<std::pair<char, char>> ranges;
    std::vector<NodePtr> children;
    NodePtr child;
    int minimum { 0 };
    int maximum { -1 };
    bool greedy { true };
    int group_index { 0 }; // 1-based capture index, 0 = non-capturing
};

NodePtr make_node(Node::Kind kind)
{
    auto node = std::make_shared<Node>();
    node->kind = kind;
    return node;
}

bool equal_char(char a, char b, bool ignore_case)
{
    if (!ignore_case)
        return a == b;
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

bool in_class(Node const& node, char c, bool ignore_case)
{
    bool found = false;
    for (auto const& [low, high] : node.ranges) {
        char value = c;
        char lo = low;
        char hi = high;
        if (ignore_case) {
            value = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            lo = static_cast<char>(std::tolower(static_cast<unsigned char>(low)));
            hi = static_cast<char>(std::tolower(static_cast<unsigned char>(high)));
        }
        if (value >= lo && value <= hi) {
            found = true;
            break;
        }
    }
    return node.negated ? !found : found;
}

bool match_node(NodePtr const& node, std::string const& text, std::size_t position, bool ignore_case, std::vector<std::pair<std::size_t, std::size_t>>* captures, std::function<bool(std::size_t)> const& continuation)
{
    switch (node->kind) {
    case Node::Kind::Literal:
        if (position < text.size() && equal_char(text[position], node->literal, ignore_case))
            return continuation(position + 1);
        return false;
    case Node::Kind::Any:
        if (position < text.size() && text[position] != '\n')
            return continuation(position + 1);
        return false;
    case Node::Kind::Class:
        if (position < text.size() && in_class(*node, text[position], ignore_case))
            return continuation(position + 1);
        return false;
    case Node::Kind::Start:
        return position == 0 && continuation(position);
    case Node::Kind::End:
        return position == text.size() && continuation(position);
    case Node::Kind::Group: {
        if (node->group_index > 0 && captures) {
            std::size_t start = position;
            auto saved = (*captures)[node->group_index];
            return match_node(node->child, text, position, ignore_case, captures, [&](std::size_t next) {
                (*captures)[node->group_index] = { start, next };
                if (continuation(next))
                    return true;
                (*captures)[node->group_index] = saved;
                return false;
            });
        }
        return match_node(node->child, text, position, ignore_case, captures, continuation);
    }
    case Node::Kind::BackReference: {
        if (!captures)
            return false;
        auto index = static_cast<std::size_t>(node->group_index);
        if (index >= captures->size())
            return false;
        auto captured = (*captures)[index];
        if (captured.first == std::string::npos)
            return false;
        auto length = captured.second - captured.first;
        if (position + length > text.size())
            return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!equal_char(text[position + i], text[captured.first + i], ignore_case))
                return false;
        return continuation(position + length);
    }
    case Node::Kind::Lookahead: {
        bool matched = match_node(node->child, text, position, ignore_case, captures, [](std::size_t) { return true; });
        if (matched == node->negated)
            return false;
        return continuation(position);
    }
    case Node::Kind::Lookbehind: {
        bool matched = false;
        for (std::size_t start = 0; start <= position && !matched; ++start)
            matched = match_node(node->child, text, start, ignore_case, captures, [&](std::size_t end) { return end == position; });
        if (matched == node->negated)
            return false;
        return continuation(position);
    }
    case Node::Kind::Concat: {
        std::function<bool(std::size_t, std::size_t)> step = [&](std::size_t index, std::size_t at) -> bool {
            if (index == node->children.size())
                return continuation(at);
            return match_node(node->children[index], text, at, ignore_case, captures, [&](std::size_t next) { return step(index + 1, next); });
        };
        return step(0, position);
    }
    case Node::Kind::Alt:
        for (auto const& branch : node->children)
            if (match_node(branch, text, position, ignore_case, captures, continuation))
                return true;
        return false;
    case Node::Kind::Repeat: {
        std::function<bool(std::size_t, int)> repeat = [&](std::size_t at, int count) -> bool {
            bool can_more = node->maximum < 0 || count < node->maximum;
            if (count < node->minimum) {
                if (!can_more)
                    return false;
                return match_node(node->child, text, at, ignore_case, captures, [&](std::size_t next) {
                    if (next == at)
                        return false;
                    return repeat(next, count + 1);
                });
            }
            if (node->greedy) {
                if (can_more && match_node(node->child, text, at, ignore_case, captures, [&](std::size_t next) {
                        if (next == at)
                            return false;
                        return repeat(next, count + 1);
                    }))
                    return true;
                return continuation(at);
            }
            if (continuation(at))
                return true;
            if (can_more)
                return match_node(node->child, text, at, ignore_case, captures, [&](std::size_t next) {
                    if (next == at)
                        return false;
                    return repeat(next, count + 1);
                });
            return false;
        };
        return repeat(position, 0);
    }
    }
    return false;
}

// Parses a pattern into a node tree. Malformed patterns set `ok` to false.
struct Parser {
    std::string const& pattern;
    std::size_t position { 0 };
    bool ok { true };
    int group_count { 0 };

    bool at_end() const { return position >= pattern.size(); }
    char peek() const { return at_end() ? '\0' : pattern[position]; }
    bool consume(char c)
    {
        if (peek() == c) {
            ++position;
            return true;
        }
        return false;
    }

    bool parse_int(int& out)
    {
        if (!std::isdigit(static_cast<unsigned char>(peek())))
            return false;
        int value = 0;
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            value = value * 10 + (peek() - '0');
            ++position;
        }
        out = value;
        return true;
    }

    NodePtr parse_alternation()
    {
        std::vector<NodePtr> branches;
        branches.push_back(parse_concatenation());
        while (consume('|'))
            branches.push_back(parse_concatenation());
        if (branches.size() == 1)
            return branches[0];
        auto node = make_node(Node::Kind::Alt);
        node->children = std::move(branches);
        return node;
    }

    NodePtr parse_concatenation()
    {
        std::vector<NodePtr> parts;
        while (!at_end() && peek() != '|' && peek() != ')')
            parts.push_back(parse_repetition());
        if (parts.size() == 1)
            return parts[0];
        auto node = make_node(Node::Kind::Concat);
        node->children = std::move(parts);
        return node;
    }

    NodePtr parse_repetition()
    {
        auto atom = parse_atom();
        while (!at_end()) {
            char c = peek();
            int minimum = 0;
            int maximum = -1;
            if (c == '*') {
                ++position;
            } else if (c == '+') {
                ++position;
                minimum = 1;
            } else if (c == '?') {
                ++position;
                maximum = 1;
            } else if (c == '{') {
                std::size_t saved = position;
                ++position;
                if (!parse_int(minimum)) {
                    position = saved;
                    break;
                }
                if (consume(',')) {
                    if (!parse_int(maximum))
                        maximum = -1;
                } else {
                    maximum = minimum;
                }
                if (!consume('}')) {
                    position = saved;
                    break;
                }
            } else {
                break;
            }
            auto repeat = make_node(Node::Kind::Repeat);
            repeat->child = atom;
            repeat->minimum = minimum;
            repeat->maximum = maximum;
            if (consume('?'))
                repeat->greedy = false;
            atom = repeat;
        }
        return atom;
    }

    NodePtr class_for_escape(char escaped)
    {
        auto node = make_node(Node::Kind::Class);
        switch (escaped) {
        case 'd': node->ranges = { { '0', '9' } }; return node;
        case 'D': node->negated = true; node->ranges = { { '0', '9' } }; return node;
        case 'w': node->ranges = { { 'a', 'z' }, { 'A', 'Z' }, { '0', '9' }, { '_', '_' } }; return node;
        case 'W': node->negated = true; node->ranges = { { 'a', 'z' }, { 'A', 'Z' }, { '0', '9' }, { '_', '_' } }; return node;
        case 's': node->ranges = { { ' ', ' ' }, { '\t', '\t' }, { '\n', '\n' }, { '\r', '\r' }, { '\f', '\f' }, { '\v', '\v' } }; return node;
        case 'S': node->negated = true; node->ranges = { { ' ', ' ' }, { '\t', '\t' }, { '\n', '\n' }, { '\r', '\r' }, { '\f', '\f' }, { '\v', '\v' } }; return node;
        case 'n': { auto literal = make_node(Node::Kind::Literal); literal->literal = '\n'; return literal; }
        case 't': { auto literal = make_node(Node::Kind::Literal); literal->literal = '\t'; return literal; }
        case 'r': { auto literal = make_node(Node::Kind::Literal); literal->literal = '\r'; return literal; }
        default: { auto literal = make_node(Node::Kind::Literal); literal->literal = escaped; return literal; }
        }
    }

    NodePtr parse_class()
    {
        ++position; // '['
        auto node = make_node(Node::Kind::Class);
        if (consume('^'))
            node->negated = true;
        while (!at_end() && peek() != ']') {
            char low = peek();
            ++position;
            if (low == '\\') {
                char escaped = peek();
                ++position;
                if (escaped == 'd') { node->ranges.push_back({ '0', '9' }); continue; }
                if (escaped == 'w') { node->ranges.push_back({ 'a', 'z' }); node->ranges.push_back({ 'A', 'Z' }); node->ranges.push_back({ '0', '9' }); node->ranges.push_back({ '_', '_' }); continue; }
                if (escaped == 's') { node->ranges.push_back({ ' ', ' ' }); node->ranges.push_back({ '\t', '\t' }); continue; }
                if (escaped == 'n') low = '\n';
                else if (escaped == 't') low = '\t';
                else if (escaped == 'r') low = '\r';
                else low = escaped;
            }
            if (peek() == '-' && position + 1 < pattern.size() && pattern[position + 1] != ']') {
                ++position; // '-'
                char high = peek();
                ++position;
                if (high == '\\') {
                    char escaped = peek();
                    ++position;
                    high = escaped;
                }
                node->ranges.push_back({ low, high });
            } else {
                node->ranges.push_back({ low, low });
            }
        }
        if (!consume(']'))
            ok = false;
        return node;
    }

    NodePtr parse_atom()
    {
        char c = peek();
        if (c == '(') {
            ++position;
            bool capturing = true;
            bool lookahead = false;
            bool lookbehind = false;
            bool negative = false;
            if (peek() == '?') {
                ++position;
                if (consume(':')) {
                    capturing = false;
                } else if (consume('=')) {
                    capturing = false;
                    lookahead = true;
                } else if (consume('!')) {
                    capturing = false;
                    lookahead = true;
                    negative = true;
                } else if (consume('<')) {
                    if (consume('=')) {
                        capturing = false;
                        lookbehind = true;
                    } else if (consume('!')) {
                        capturing = false;
                        lookbehind = true;
                        negative = true;
                    } else {
                        ok = false; // named groups are unsupported
                    }
                } else {
                    ok = false;
                }
            }
            auto inner = parse_alternation();
            if (!consume(')'))
                ok = false;
            if (lookahead || lookbehind) {
                auto node = make_node(lookahead ? Node::Kind::Lookahead : Node::Kind::Lookbehind);
                node->child = inner;
                node->negated = negative;
                return node;
            }
            auto group = make_node(Node::Kind::Group);
            group->child = inner;
            if (capturing)
                group->group_index = ++group_count;
            return group;
        }
        if (c == '[')
            return parse_class();
        if (c == '.') { ++position; return make_node(Node::Kind::Any); }
        if (c == '^') { ++position; return make_node(Node::Kind::Start); }
        if (c == '$') { ++position; return make_node(Node::Kind::End); }
        if (c == '\\') {
            ++position;
            char escaped = peek();
            ++position;
            if (escaped >= '1' && escaped <= '9') {
                auto node = make_node(Node::Kind::BackReference);
                node->group_index = escaped - '0';
                return node;
            }
            return class_for_escape(escaped);
        }
        if (c == '\0') {
            ok = false;
            return make_node(Node::Kind::Literal);
        }
        ++position;
        auto literal = make_node(Node::Kind::Literal);
        literal->literal = c;
        return literal;
    }
};

} // namespace

struct Regex::Impl {
    std::string source;
    std::string flags;
    bool global { false };
    bool ignore_case { false };
    std::size_t group_count { 0 };
    NodePtr root;
};

std::shared_ptr<Regex> Regex::compile(std::string const& pattern, std::string const& flags, std::string& error)
{
    auto impl = std::make_shared<Impl>();
    impl->source = pattern;
    impl->flags = flags;
    for (char flag : flags) {
        if (flag == 'g')
            impl->global = true;
        else if (flag == 'i')
            impl->ignore_case = true;
        else if (flag == 'm')
            ; // multiline is accepted but treated as single-line
        else {
            error = "invalid regular expression flag";
            return nullptr;
        }
    }

    Parser parser { pattern, 0, true };
    impl->root = parser.parse_alternation();
    if (parser.ok && parser.position != pattern.size()) {
        // Trailing characters (e.g. an unmatched ')') are a syntax error.
        if (parser.peek() == ')')
            parser.ok = false;
    }
    if (!parser.ok) {
        error = "invalid regular expression";
        return nullptr;
    }
    impl->group_count = static_cast<std::size_t>(parser.group_count);

    auto regex = std::make_shared<Regex>();
    regex->m_impl = impl;
    return regex;
}

bool Regex::search(std::string const& text, std::size_t start, std::size_t& match_start, std::size_t& match_end,
    std::vector<std::pair<std::size_t, std::size_t>>* captures) const
{
    if (!m_impl || !m_impl->root)
        return false;
    for (std::size_t position = start; position <= text.size(); ++position) {
        std::size_t end = position;
        std::vector<std::pair<std::size_t, std::size_t>> slots(m_impl->group_count + 1, { std::string::npos, std::string::npos });
        bool matched = match_node(m_impl->root, text, position, m_impl->ignore_case, &slots, [&](std::size_t at) {
            end = at;
            return true;
        });
        if (matched) {
            match_start = position;
            match_end = end;
            if (captures)
                *captures = slots;
            return true;
        }
    }
    return false;
}

std::size_t Regex::group_count() const { return m_impl ? m_impl->group_count : 0; }

bool Regex::test(std::string const& text) const
{
    std::size_t start = 0;
    std::size_t end = 0;
    return search(text, 0, start, end);
}

std::string const& Regex::source() const { return m_impl->source; }
std::string const& Regex::flags() const { return m_impl->flags; }
bool Regex::global() const { return m_impl->global; }
bool Regex::ignore_case() const { return m_impl->ignore_case; }

} // namespace aetheris::js
