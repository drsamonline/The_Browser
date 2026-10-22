#include "js.hpp"
#include "regex.hpp"

#include <chrono>
#include <ctime>
#include <cstdio>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace aetheris::js {

// ---------------------------------------------------------------------------
// Value conversions
// ---------------------------------------------------------------------------

bool Value::to_boolean() const
{
    switch (type) {
    case Type::Undefined:
    case Type::Null: return false;
    case Type::Boolean: return boolean;
    case Type::Number: return number != 0 && !std::isnan(number);
    case Type::String: return !string.empty();
    case Type::Object: return true;
    }
    return false;
}

double Value::to_number() const
{
    switch (type) {
    case Type::Undefined: return std::nan("");
    case Type::Null: return 0;
    case Type::Boolean: return boolean ? 1 : 0;
    case Type::Number: return number;
    case Type::String: {
        if (string.empty()) return 0;
        char* end = nullptr;
        double value = std::strtod(string.c_str(), &end);
        return (end == string.c_str()) ? std::nan("") : value;
    }
    case Type::Object: return std::nan("");
    }
    return std::nan("");
}

static std::string number_to_string(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0)
        return "0";
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        std::ostringstream stream;
        stream << static_cast<long long>(value);
        return stream.str();
    }
    std::ostringstream stream;
    stream.precision(15);
    stream << value;
    return stream.str();
}

std::string Value::to_string() const
{
    switch (type) {
    case Type::Undefined: return "undefined";
    case Type::Null: return "null";
    case Type::Boolean: return boolean ? "true" : "false";
    case Type::Number: return number_to_string(number);
    case Type::String: return string;
    case Type::Object:
        if (object && object->is_array) {
            std::string result = "[";
            for (size_t i = 0; i < object->elements.size(); ++i) {
                if (i) result += ",";
                if (!object->elements[i].is_undefined())
                    result += object->elements[i].to_string();
            }
            return result + "]";
        }
        if (object && object->is_function) return "function";
        return "[object Object]";
    }
    return {};
}

bool Value::strict_equals(Value const& other) const
{
    if (type != other.type)
        return false;
    switch (type) {
    case Type::Undefined:
    case Type::Null: return true;
    case Type::Boolean: return boolean == other.boolean;
    case Type::Number: return number == other.number;
    case Type::String: return string == other.string;
    case Type::Object: return object == other.object;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

namespace {

bool is_identifier_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool is_identifier_part(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

bool is_keyword(std::string const& text)
{
    static const char* keywords[] = {
        "var", "let", "const", "function", "return", "if", "else", "while", "for",
        "break", "continue", "true", "false", "null", "undefined", "new", "typeof", "else", "instanceof", "class", "switch", "case", "default", "of", "async", "await"
    };
    for (auto* keyword : keywords)
        if (text == keyword)
            return true;
    return false;
}

} // namespace

Lexer::Lexer(std::string source)
    : m_source(std::move(source))
{
}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < m_source.size()) {
        char c = m_source[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // Comments
        if (c == '/' && i + 1 < m_source.size() && m_source[i + 1] == '/') {
            while (i < m_source.size() && m_source[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < m_source.size() && m_source[i + 1] == '*') {
            i += 2;
            while (i + 1 < m_source.size() && !(m_source[i] == '*' && m_source[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }

        // Regular-expression literal: a '/' where a value is expected (not
        // after a token that can end an expression) starts a regex.
        if (c == '/') {
            bool value_expected = true;
            if (!tokens.empty()) {
                TokenType previous = tokens.back().type;
                if (previous == TokenType::Identifier || previous == TokenType::Number || previous == TokenType::String || previous == TokenType::Regex
                    || (previous == TokenType::Punctuator && (tokens.back().text == ")" || tokens.back().text == "]" || tokens.back().text == "}")))
                    value_expected = false;
            }
            if (value_expected) {
                size_t j = i + 1;
                bool in_class = false;
                while (j < m_source.size()) {
                    char rc = m_source[j];
                    if (rc == '\\') { j += 2; continue; }
                    if (rc == '[') in_class = true;
                    else if (rc == ']') in_class = false;
                    else if (rc == '/' && !in_class) break;
                    ++j;
                }
                if (j < m_source.size()) {
                    Token token;
                    token.type = TokenType::Regex;
                    token.text = m_source.substr(i + 1, j - (i + 1));
                    size_t k = j + 1;
                    while (k < m_source.size() && std::isalpha(static_cast<unsigned char>(m_source[k]))) ++k;
                    token.regex_flags = m_source.substr(j + 1, k - (j + 1));
                    i = k;
                    tokens.push_back(std::move(token));
                    continue;
                }
            }
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < m_source.size() && std::isdigit(static_cast<unsigned char>(m_source[i + 1])))) {
            size_t start = i;
            while (i < m_source.size() && (std::isdigit(static_cast<unsigned char>(m_source[i])) || m_source[i] == '.' || m_source[i] == 'e' || m_source[i] == 'E' || m_source[i] == '+' || m_source[i] == '-')) {
                if ((m_source[i] == '+' || m_source[i] == '-') && !(i > start && (m_source[i - 1] == 'e' || m_source[i - 1] == 'E')))
                    break;
                ++i;
            }
            Token token;
            token.type = TokenType::Number;
            token.text = m_source.substr(start, i - start);
            token.number = std::strtod(token.text.c_str(), nullptr);
            tokens.push_back(std::move(token));
            continue;
        }

        if (c == '"' || c == '\'') {
            char quote = c;
            ++i;
            std::string value;
            while (i < m_source.size() && m_source[i] != quote) {
                if (m_source[i] == '\\' && i + 1 < m_source.size()) {
                    ++i;
                    char escaped = m_source[i];
                    switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '\'': value += '\''; break;
                    case '"': value += '"'; break;
                    default: value += escaped; break;
                    }
                } else {
                    value += m_source[i];
                }
                ++i;
            }
            ++i; // closing quote
            Token token;
            token.type = TokenType::String;
            token.text = value;
            tokens.push_back(std::move(token));
            continue;
        }

        if (is_identifier_start(c)) {
            size_t start = i;
            while (i < m_source.size() && is_identifier_part(m_source[i])) ++i;
            Token token;
            token.text = m_source.substr(start, i - start);
            token.type = is_keyword(token.text) ? TokenType::Keyword : TokenType::Identifier;
            tokens.push_back(std::move(token));
            continue;
        }

        // Punctuators (longest match first).
        static const char* punctuators[] = {
            "===", "!==", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=",
            "=", "+", "-", "*", "/", "%", "<", ">", "!", "(", ")", "{", "}", "[", "]", ",", ";", ":", ".", "?"
        };
        bool matched = false;
        for (auto* punctuator : punctuators) {
            size_t length = std::strlen(punctuator);
            if (m_source.compare(i, length, punctuator) == 0) {
                Token token;
                token.type = TokenType::Punctuator;
                token.text = punctuator;
                tokens.push_back(std::move(token));
                i += length;
                matched = true;
                break;
            }
        }
        if (!matched)
            ++i; // skip unknown character
    }
    Token end;
    end.type = TokenType::EndOfInput;
    tokens.push_back(std::move(end));
    return tokens;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

Parser::Parser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens))
{
}

Token const& Parser::peek(int offset) const
{
    size_t index = m_position + offset;
    if (index >= m_tokens.size())
        index = m_tokens.size() - 1;
    return m_tokens[index];
}

Token const& Parser::advance()
{
    Token const& token = peek();
    if (m_position + 1 < m_tokens.size())
        ++m_position;
    return token;
}

bool Parser::check(char const* text) const
{
    // Only punctuators and keywords can stand in for operators; a string or
    // number token whose text happens to match (e.g. "-") must not.
    auto const& token = peek();
    if (token.type != TokenType::Punctuator && token.type != TokenType::Keyword)
        return false;
    return token.text == text;
}

bool Parser::match(char const* text)
{
    if (check(text)) { advance(); return true; }
    return false;
}

bool Parser::expect(char const* text)
{
    if (match(text))
        return true;
    set_error(std::string("expected '") + text + "'");
    return false;
}

void Parser::set_error(std::string message)
{
    if (m_error.empty())
        m_error = std::move(message);
}

static std::unique_ptr<Node> make_node(NodeType type)
{
    auto node = std::make_unique<Node>();
    node->type = type;
    return node;
}

std::unique_ptr<Node> Parser::parse_program()
{
    auto program = make_node(NodeType::Program);
    while (peek().type != TokenType::EndOfInput && !failed()) {
        auto statement = parse_statement();
        if (statement)
            program->children.push_back(std::move(statement));
    }
    return program;
}

std::unique_ptr<Node> Parser::parse_block()
{
    auto block = make_node(NodeType::Block);
    if (!expect("{"))
        return block;
    while (!check("}") && peek().type != TokenType::EndOfInput && !failed()) {
        auto statement = parse_statement();
        if (statement)
            block->children.push_back(std::move(statement));
    }
    expect("}");
    return block;
}

std::unique_ptr<Node> Parser::parse_statement()
{
    if (check("{"))
        return parse_block();

    if (check("var") || check("let") || check("const")) {
        bool is_let = !check("var");
        advance();
        auto statement = make_node(NodeType::VariableDeclaration);
        statement->kind_let = is_let;
        for (;;) {
            if (peek().type != TokenType::Identifier) { set_error("expected variable name"); break; }
            statement->names.push_back(advance().text);
            if (match("="))
                statement->initializers.push_back(parse_assignment());
            else
                statement->initializers.push_back(nullptr);
            if (!match(","))
                break;
        }
        match(";");
        return statement;
    }

    if (check("class")) {
        advance();
        return parse_class();
    }

    if (check("async") && peek(1).text == "function") {
        advance();
        advance();
        auto function = parse_function(true);
        function->boolean = true; // async
        return function;
    }

    if (check("function")) {
        advance();
        return parse_function(true);
    }

    if (match("if")) {
        auto statement = make_node(NodeType::IfStatement);
        expect("(");
        statement->test = parse_expression();
        expect(")");
        statement->body = parse_statement();
        if (match("else"))
            statement->alternate = parse_statement();
        return statement;
    }

    if (match("while")) {
        auto statement = make_node(NodeType::WhileStatement);
        expect("(");
        statement->test = parse_expression();
        expect(")");
        statement->body = parse_statement();
        return statement;
    }

    if (match("for")) {
        expect("(");
        // for (var x of iterable) { ... }
        if (check("var") || check("let") || check("const")) {
            size_t saved = m_position;
            advance();
            if (peek().type == TokenType::Identifier) {
                std::string name = peek().text;
                advance();
                if (check("of")) {
                    advance();
                    auto for_of = make_node(NodeType::ForOfStatement);
                    for_of->string = name;
                    for_of->right = parse_expression();
                    expect(")");
                    for_of->body = parse_statement();
                    return for_of;
                }
            }
            m_position = saved;
        }
        auto statement = make_node(NodeType::ForStatement);
        if (check("var") || check("let")) {
            statement->init = parse_statement();
        } else if (!check(";")) {
            auto expression = make_node(NodeType::ExpressionStatement);
            expression->left = parse_expression();
            statement->init = std::move(expression);
            match(";");
        } else {
            match(";");
        }
        if (!check(";"))
            statement->test = parse_expression();
        expect(";");
        if (!check(")"))
            statement->update = parse_expression();
        expect(")");
        statement->body = parse_statement();
        return statement;
    }

    if (match("return")) {
        auto statement = make_node(NodeType::ReturnStatement);
        if (!check(";") && !check("}") && peek().type != TokenType::EndOfInput)
            statement->left = parse_expression();
        match(";");
        return statement;
    }

    if (match("break")) { match(";"); return make_node(NodeType::BreakStatement); }
    if (match("continue")) { match(";"); return make_node(NodeType::ContinueStatement); }

    if (match("switch")) {
        auto statement = make_node(NodeType::SwitchStatement);
        expect("(");
        statement->test = parse_expression();
        expect(")");
        expect("{");
        while (!check("}") && peek().type != TokenType::EndOfInput && !failed()) {
            std::unique_ptr<Node> case_expression;
            if (match("case")) { case_expression = parse_expression(); expect(":"); }
            else if (match("default")) { expect(":"); }
            else { set_error("expected 'case' or 'default'"); break; }
            auto block = make_node(NodeType::Block);
            while (!check("case") && !check("default") && !check("}") && peek().type != TokenType::EndOfInput)
                block->children.push_back(parse_statement());
            statement->initializers.push_back(std::move(case_expression));
            statement->children.push_back(std::move(block));
        }
        expect("}");
        return statement;
    }

    auto statement = make_node(NodeType::ExpressionStatement);
    statement->left = parse_expression();
    match(";");
    return statement;
}

std::unique_ptr<Node> Parser::parse_function(bool is_declaration)
{
    auto function = make_node(is_declaration ? NodeType::FunctionDeclaration : NodeType::FunctionExpression);
    if (is_declaration) {
        if (peek().type != TokenType::Identifier) { set_error("expected function name"); return function; }
        function->string = advance().text;
    } else if (peek().type == TokenType::Identifier) {
        function->string = advance().text;
    }
    if (!expect("("))
        return function;
    while (!check(")") && peek().type != TokenType::EndOfInput) {
        if (peek().type != TokenType::Identifier) { set_error("expected parameter name"); break; }
        function->parameters.push_back(advance().text);
        if (!match(","))
            break;
    }
    expect(")");
    function->body = parse_block();
    return function;
}

std::unique_ptr<Node> Parser::parse_class()
{
    auto node = make_node(NodeType::ClassDeclaration);
    if (peek().type != TokenType::Identifier) { set_error("expected class name"); return node; }
    node->string = advance().text;
    if (!expect("{"))
        return node;
    while (!check("}") && peek().type != TokenType::EndOfInput && !failed()) {
        if (peek().type != TokenType::Identifier && peek().type != TokenType::Keyword) { set_error("expected method name"); break; }
        std::string method_name = advance().text;
        auto function = parse_function(false);
        if (method_name == "constructor")
            node->left = std::move(function);
        else
            node->properties.emplace_back(method_name, std::move(function));
    }
    expect("}");
    return node;
}

std::unique_ptr<Node> Parser::parse_expression()
{
    return parse_assignment();
}

std::unique_ptr<Node> Parser::parse_assignment()
{
    auto left = parse_conditional();
    if (check("=") || check("+=") || check("-=") || check("*=") || check("/=") || check("%=")) {
        std::string op = advance().text;
        auto node = make_node(NodeType::Assignment);
        node->string = op;
        node->left = std::move(left);
        node->right = parse_assignment();
        return node;
    }
    return left;
}

std::unique_ptr<Node> Parser::parse_conditional()
{
    auto condition = parse_binary(1);
    if (match("?")) {
        auto node = make_node(NodeType::Conditional);
        node->left = std::move(condition);
        node->right = parse_assignment();
        expect(":");
        node->third = parse_assignment();
        return node;
    }
    return condition;
}

static int binary_precedence(std::string const& op)
{
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "===" || op == "!==") return 3;
    if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "instanceof") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0;
}

std::unique_ptr<Node> Parser::parse_binary(int min_precedence)
{
    auto left = parse_unary();
    for (;;) {
        if (peek().type != TokenType::Punctuator && peek().type != TokenType::Keyword)
            break;
        std::string op = peek().text;
        int precedence = binary_precedence(op);
        if (precedence == 0 || precedence < min_precedence)
            break;
        advance();
        auto right = parse_binary(precedence + 1);
        auto node = make_node(op == "&&" || op == "||" ? NodeType::Logical : NodeType::Binary);
        node->string = op;
        node->left = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Node> Parser::parse_unary()
{
    if (check("new")) {
        advance();
        auto node = make_node(NodeType::New);
        // `new` binds tighter than a call: the callee is a primary with member
        // and index accesses, followed by at most one argument list. This makes
        // `new X(a).y()` parse as `(new X(a)).y()`.
        auto callee = parse_primary();
        while (!failed()) {
            if (match(".")) {
                auto member = make_node(NodeType::Member);
                member->left = std::move(callee);
                if (peek().type == TokenType::Identifier || peek().type == TokenType::Keyword)
                    member->string = advance().text;
                else
                    set_error("expected property name");
                callee = std::move(member);
            } else if (match("[")) {
                auto member = make_node(NodeType::Member);
                member->left = std::move(callee);
                member->right = parse_expression();
                expect("]");
                callee = std::move(member);
            } else {
                break;
            }
        }
        if (check("(")) {
            advance();
            while (!check(")") && peek().type != TokenType::EndOfInput && !failed()) {
                node->children.push_back(parse_assignment());
                if (!match(","))
                    break;
            }
            expect(")");
        }
        node->left = std::move(callee);
        return apply_postfix(std::move(node));
    }
    if (check("await")) {
        advance();
        auto node = make_node(NodeType::Unary);
        node->string = "await";
        node->left = parse_unary();
        return node;
    }
    if (check("!") || check("-") || check("+") || check("typeof")) {
        std::string op = advance().text;
        auto node = make_node(NodeType::Unary);
        node->string = op;
        node->left = parse_unary();
        return node;
    }
    if (check("++") || check("--")) {
        std::string op = advance().text;
        auto node = make_node(NodeType::Unary);
        node->string = op; // prefix
        node->left = parse_unary();
        return node;
    }
    return parse_postfix();
}

std::unique_ptr<Node> Parser::apply_postfix(std::unique_ptr<Node> expression)
{
    for (;;) {
        if (match(".")) {
            if (peek().type != TokenType::Identifier && peek().type != TokenType::Keyword) { set_error("expected property name"); break; }
            auto node = make_node(NodeType::Member);
            node->left = std::move(expression);
            node->string = advance().text;
            expression = std::move(node);
        } else if (check("[")) {
            advance();
            auto node = make_node(NodeType::Member);
            node->left = std::move(expression);
            node->right = parse_expression();
            expect("]");
            expression = std::move(node);
        } else if (check("(")) {
            advance();
            auto node = make_node(NodeType::Call);
            node->left = std::move(expression);
            while (!check(")") && peek().type != TokenType::EndOfInput) {
                node->children.push_back(parse_assignment());
                if (!match(","))
                    break;
            }
            expect(")");
            expression = std::move(node);
        } else if (check("++") || check("--")) {
            std::string op = advance().text;
            auto node = make_node(NodeType::Unary);
            node->string = op; // postfix
            node->left = std::move(expression);
            expression = std::move(node);
        } else {
            break;
        }
    }
    return expression;
}

std::unique_ptr<Node> Parser::parse_postfix()
{
    return apply_postfix(parse_primary());
}

std::unique_ptr<Node> Parser::parse_primary()
{
    Token const& token = peek();

    if (token.type == TokenType::Number) {
        advance();
        auto node = make_node(NodeType::NumberLiteral);
        node->number = token.number;
        return node;
    }
    if (token.type == TokenType::Regex) {
        advance();
        auto node = make_node(NodeType::RegexLiteral);
        std::string regex_error;
        node->regex = Regex::compile(token.text, token.regex_flags, regex_error);
        if (!node->regex)
            set_error(regex_error.empty() ? "invalid regular expression" : regex_error);
        return node;
    }
    if (token.type == TokenType::String) {
        advance();
        auto node = make_node(NodeType::StringLiteral);
        node->string = token.text;
        return node;
    }
    if (token.text == "true" || token.text == "false") {
        advance();
        auto node = make_node(NodeType::BooleanLiteral);
        node->boolean = token.text == "true";
        return node;
    }
    if (token.text == "null") { advance(); return make_node(NodeType::NullLiteral); }
    if (token.text == "undefined") { advance(); auto node = make_node(NodeType::Identifier); node->string = "undefined"; return node; }
    if (token.text == "function") { advance(); return parse_function(false); }

    if (token.type == TokenType::Identifier || token.type == TokenType::Keyword) {
        advance();
        auto node = make_node(NodeType::Identifier);
        node->string = token.text;
        return node;
    }

    if (check("(")) {
        advance();
        auto expression = parse_expression();
        expect(")");
        return expression;
    }

    if (check("[")) {
        advance();
        auto node = make_node(NodeType::ArrayLiteral);
        while (!check("]") && peek().type != TokenType::EndOfInput) {
            node->children.push_back(parse_assignment());
            if (!match(","))
                break;
        }
        expect("]");
        return node;
    }

    if (check("{")) {
        advance();
        auto node = make_node(NodeType::ObjectLiteral);
        while (!check("}") && peek().type != TokenType::EndOfInput) {
            std::string key;
            if (peek().type == TokenType::Identifier || peek().type == TokenType::Keyword || peek().type == TokenType::String)
                key = advance().text;
            else { set_error("expected property key"); break; }
            expect(":");
            node->properties.emplace_back(key, parse_assignment());
            if (!match(","))
                break;
        }
        expect("}");
        return node;
    }

    set_error("unexpected token '" + token.text + "'");
    advance();
    return make_node(NodeType::NullLiteral);
}

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------

Value* Environment::find(std::string const& name)
{
    for (Environment* env = this; env; env = env->parent.get()) {
        auto it = env->variables.find(name);
        if (it != env->variables.end())
            return &it->second;
    }
    return nullptr;
}

Interpreter::Interpreter()
{
    m_globals = std::make_shared<Environment>();
    setup_globals();
}

void Interpreter::error(std::string message)
{
    if (!m_has_error) {
        m_has_error = true;
        m_error = std::move(message);
    }
}

void Interpreter::declare(std::shared_ptr<Environment> const& env, std::string const& name, Value value)
{
    env->variables[name] = std::move(value);
}

static std::shared_ptr<Object> make_native(NativeFunction function)
{
    auto object = std::make_shared<Object>();
    object->is_function = true;
    object->is_native = true;
    object->native = std::move(function);
    return object;
}

// ---------------------------------------------------------------------------
// JSON (stringify + parse), following the ECMAScript JSON object semantics.
// ---------------------------------------------------------------------------

void json_escape(std::string const& value, std::string& out)
{
    out += '"';
    for (char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default: out += c; break;
        }
    }
    out += '"';
}

void json_write(Value const& value, std::string& out)
{
    switch (value.type) {
    case Value::Type::Undefined:
    case Value::Type::Null:
        out += "null";
        break;
    case Value::Type::Boolean:
        out += value.boolean ? "true" : "false";
        break;
    case Value::Type::Number:
        out += number_to_string(value.number);
        break;
    case Value::Type::String:
        json_escape(value.string, out);
        break;
    case Value::Type::Object:
        if (!value.object || value.object->is_function) {
            out += "null";
            break;
        }
        if (value.object->is_array) {
            out += '[';
            for (size_t i = 0; i < value.object->elements.size(); ++i) {
                if (i) out += ',';
                json_write(value.object->elements[i], out);
            }
            out += ']';
        } else {
            out += '{';
            bool first = true;
            for (auto const& [key, property] : value.object->properties) {
                if (property.is_object() && property.object && property.object->is_function)
                    continue;
                if (!first) out += ',';
                first = false;
                json_escape(key, out);
                out += ':';
                json_write(property, out);
            }
            out += '}';
        }
        break;
    }
}

struct JsonParser {
    std::string const& text;
    size_t pos { 0 };
    bool ok { true };

    void skip() { while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos; }

    Value parse()
    {
        skip();
        if (pos >= text.size()) { ok = false; return {}; }
        char c = text[pos];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Value::from_string(parse_string());
        if (c == 't') { if (text.compare(pos, 4, "true") == 0) { pos += 4; return Value::from_boolean(true); } ok = false; return {}; }
        if (c == 'f') { if (text.compare(pos, 5, "false") == 0) { pos += 5; return Value::from_boolean(false); } ok = false; return {}; }
        if (c == 'n') { if (text.compare(pos, 4, "null") == 0) { pos += 4; return Value::null(); } ok = false; return {}; }
        char* end = nullptr;
        double number = std::strtod(text.c_str() + pos, &end);
        if (end == text.c_str() + pos) { ok = false; return {}; }
        pos = static_cast<size_t>(end - text.c_str());
        return Value::from_number(number);
    }

    std::string parse_string()
    {
        std::string result;
        ++pos; // opening quote
        while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) {
                ++pos;
                switch (text[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                default: result += text[pos]; break;
                }
            } else {
                result += text[pos];
            }
            ++pos;
        }
        ++pos; // closing quote
        return result;
    }

    Value parse_object()
    {
        auto object = std::make_shared<Object>();
        ++pos;
        skip();
        if (pos < text.size() && text[pos] == '}') { ++pos; return Value::from_object(object); }
        while (pos < text.size()) {
            skip();
            if (text[pos] != '"') { ok = false; break; }
            std::string key = parse_string();
            skip();
            if (pos >= text.size() || text[pos] != ':') { ok = false; break; }
            ++pos;
            object->properties[key] = parse();
            skip();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == '}') { ++pos; break; }
            ok = false;
            break;
        }
        return Value::from_object(object);
    }

    Value parse_array()
    {
        auto array = std::make_shared<Object>();
        array->is_array = true;
        ++pos;
        skip();
        if (pos < text.size() && text[pos] == ']') { ++pos; return Value::from_object(array); }
        while (pos < text.size()) {
            array->elements.push_back(parse());
            skip();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == ']') { ++pos; break; }
            ok = false;
            break;
        }
        return Value::from_object(array);
    }
};

static Value make_regex_object(std::shared_ptr<Regex> const& regex)
{
    if (!regex)
        return Value::null();
    auto object = std::make_shared<Object>();
    object->regex = regex;
    object->properties["source"] = Value::from_string(regex->source());
    object->properties["flags"] = Value::from_string(regex->flags());
    object->properties["global"] = Value::from_boolean(regex->global());
    object->properties["test"] = Value::from_object(make_native([regex](std::vector<Value> const& args) {
        return Value::from_boolean(!args.empty() && regex->test(args[0].to_string()));
    }));
    object->properties["exec"] = Value::from_object(make_native([regex](std::vector<Value> const& args) {
        std::string text = args.empty() ? std::string() : args[0].to_string();
        std::size_t start = 0, end = 0;
        if (!regex->search(text, 0, start, end))
            return Value::null();
        auto array = std::make_shared<Object>();
        array->is_array = true;
        array->elements.push_back(Value::from_string(text.substr(start, end - start)));
        array->properties["index"] = Value::from_number(static_cast<double>(start));
        return Value::from_object(array);
    }));
    return Value::from_object(object);
}

Value Interpreter::make_promise()
{
    auto object = std::make_shared<Object>();
    object->prototype = m_object_prototype;
    object->promise = std::make_shared<PromiseData>();
    auto self = object;
    object->properties["then"] = Value::from_object(make_native([this, self](std::vector<Value> const& args) {
        auto derived = make_promise();
        Value on_fulfilled = args.size() >= 1 ? args[0] : Value::undefined();
        Value on_rejected = args.size() >= 2 ? args[1] : Value::undefined();
        if (self->promise->status == PromiseData::Status::Pending) {
            self->promise->handlers.emplace_back(on_fulfilled, on_rejected);
        } else {
            bool rejected = self->promise->status == PromiseData::Status::Rejected;
            Value value = self->promise->value;
            schedule_microtask([this, derived, on_fulfilled, on_rejected, rejected, value]() {
                run_promise_handler(derived.object, rejected ? on_rejected : on_fulfilled, rejected, value);
            });
        }
        return derived;
    }));
    object->properties["catch"] = Value::from_object(make_native([this, self](std::vector<Value> const& args) {
        auto derived = make_promise();
        Value on_rejected = args.empty() ? Value::undefined() : args[0];
        if (self->promise->status == PromiseData::Status::Pending) {
            self->promise->handlers.emplace_back(Value::undefined(), on_rejected);
        } else if (self->promise->status == PromiseData::Status::Rejected) {
            Value value = self->promise->value;
            schedule_microtask([this, derived, on_rejected, value]() {
                run_promise_handler(derived.object, on_rejected, true, value);
            });
        } else {
            settle_promise(derived.object, false, self->promise->value);
        }
        return derived;
    }));
    object->properties["finally"] = Value::from_object(make_native([this, self](std::vector<Value> const& args) {
        if (!args.empty() && args[0].is_object() && args[0].object && args[0].object->is_function)
            call(args[0], {});
        return Value::from_object(self);
    }));
    return Value::from_object(object);
}

void Interpreter::schedule_microtask(std::function<void()> task)
{
    m_microtasks.push_back(std::move(task));
}

void Interpreter::settle_promise(std::shared_ptr<Object> const& promise, bool rejected, Value const& value)
{
    if (!promise || !promise->promise || promise->promise->status != PromiseData::Status::Pending)
        return;
    promise->promise->status = rejected ? PromiseData::Status::Rejected : PromiseData::Status::Fulfilled;
    promise->promise->value = value;
    auto handlers = std::move(promise->promise->handlers);
    promise->promise->handlers.clear();
    for (auto const& [on_fulfilled, on_rejected] : handlers) {
        schedule_microtask([this, promise, on_fulfilled, on_rejected, rejected, value]() {
            run_promise_handler(promise, rejected ? on_rejected : on_fulfilled, rejected, value);
        });
    }
}

void Interpreter::run_promise_handler(std::shared_ptr<Object> const& derived, Value const& handler, bool rejected, Value const& value)
{
    if (!derived)
        return;
    if (!handler.is_object() || !handler.object || !handler.object->is_function) {
        settle_promise(derived, rejected, value);
        return;
    }
    auto result = call(handler, { value });
    if (result.is_object() && result.object && result.object->promise) {
        auto returned = result.object;
        if (returned->promise->status == PromiseData::Status::Pending) {
            auto fulfill = make_native([this, derived](std::vector<Value> const& args) {
                settle_promise(derived, false, args.empty() ? Value::undefined() : args[0]);
                return Value::undefined();
            });
            auto reject = make_native([this, derived](std::vector<Value> const& args) {
                settle_promise(derived, true, args.empty() ? Value::undefined() : args[0]);
                return Value::undefined();
            });
            returned->promise->handlers.emplace_back(Value::from_object(fulfill), Value::from_object(reject));
        } else {
            settle_promise(derived, returned->promise->status == PromiseData::Status::Rejected, returned->promise->value);
        }
    } else {
        settle_promise(derived, false, result);
    }
}

void Interpreter::drain_microtasks()
{
    for (int guard = 0; guard < 1000000 && !m_microtasks.empty(); ++guard) {
        auto task = std::move(m_microtasks.front());
        m_microtasks.erase(m_microtasks.begin());
        task();
    }
}

static double current_time_ms()
{
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

static std::string format_date(double ms, bool iso)
{
    auto seconds = static_cast<std::time_t>(ms / 1000.0);
    std::tm tm {};
    gmtime_r(&seconds, &tm);
    char buffer[64];
    if (iso)
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.000Z", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    else
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

static Value make_date_object(double ms)
{
    auto object = std::make_shared<Object>();
    auto part = [ms](int which) -> double {
        auto seconds = static_cast<std::time_t>(ms / 1000.0);
        std::tm tm {};
        gmtime_r(&seconds, &tm);
        switch (which) {
        case 0: return tm.tm_year + 1900;
        case 1: return tm.tm_mon;
        case 2: return tm.tm_mday;
        case 3: return tm.tm_wday;
        case 4: return tm.tm_hour;
        case 5: return tm.tm_min;
        default: return tm.tm_sec;
        }
    };
    object->properties["getFullYear"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(0)); }));
    object->properties["getMonth"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(1)); }));
    object->properties["getDate"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(2)); }));
    object->properties["getDay"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(3)); }));
    object->properties["getHours"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(4)); }));
    object->properties["getMinutes"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(5)); }));
    object->properties["getSeconds"] = Value::from_object(make_native([part](std::vector<Value> const&) { return Value::from_number(part(6)); }));
    object->properties["getTime"] = Value::from_object(make_native([ms](std::vector<Value> const&) { return Value::from_number(ms); }));
    object->properties["toISOString"] = Value::from_object(make_native([ms](std::vector<Value> const&) { return Value::from_string(format_date(ms, true)); }));
    object->properties["toString"] = Value::from_object(make_native([ms](std::vector<Value> const&) { return Value::from_string(format_date(ms, false)); }));
    return Value::from_object(object);
}

void Interpreter::setup_globals()
{
    m_object_prototype = std::make_shared<Object>();

    // console.log
    auto console = std::make_shared<Object>();
    console->properties["log"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) line += " ";
            line += args[i].to_string();
        }
        if (print_handler)
            print_handler(line);
        return Value::undefined();
    }));
    m_globals->variables["console"] = Value::from_object(console);

    // Math
    auto math = std::make_shared<Object>();
    auto math_unary = [](double (*fn)(double)) {
        return make_native([fn](std::vector<Value> const& args) { return Value::from_number(args.empty() ? std::nan("") : fn(args[0].to_number())); });
    };
    math->properties["floor"] = Value::from_object(math_unary([](double x) { return std::floor(x); }));
    math->properties["ceil"] = Value::from_object(math_unary([](double x) { return std::ceil(x); }));
    math->properties["round"] = Value::from_object(math_unary([](double x) { return std::round(x); }));
    math->properties["abs"] = Value::from_object(math_unary([](double x) { return std::fabs(x); }));
    math->properties["sqrt"] = Value::from_object(math_unary([](double x) { return std::sqrt(x); }));
    math->properties["max"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        double result = -std::numeric_limits<double>::infinity();
        for (auto const& arg : args) result = std::max(result, arg.to_number());
        return Value::from_number(result);
    }));
    math->properties["min"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        double result = std::numeric_limits<double>::infinity();
        for (auto const& arg : args) result = std::min(result, arg.to_number());
        return Value::from_number(result);
    }));
    math->properties["pow"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        return Value::from_number(args.size() >= 2 ? std::pow(args[0].to_number(), args[1].to_number()) : std::nan(""));
    }));
    math->properties["random"] = Value::from_object(make_native([](std::vector<Value> const&) {
        return Value::from_number(static_cast<double>(std::rand()) / RAND_MAX);
    }));
    math->properties["trunc"] = Value::from_object(math_unary([](double x) { return std::trunc(x); }));
    math->properties["sign"] = Value::from_object(math_unary([](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }));
    math->properties["PI"] = Value::from_number(3.141592653589793);
    m_globals->variables["Math"] = Value::from_object(math);

    // Global functions
    m_globals->variables["parseInt"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        if (args.empty()) return Value::from_number(std::nan(""));
        return Value::from_number(static_cast<double>(std::strtol(args[0].to_string().c_str(), nullptr, 10)));
    }));
    m_globals->variables["parseFloat"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        if (args.empty()) return Value::from_number(std::nan(""));
        return Value::from_number(std::strtod(args[0].to_string().c_str(), nullptr));
    }));
    m_globals->variables["String"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        return Value::from_string(args.empty() ? std::string() : args[0].to_string());
    }));
    m_globals->variables["Number"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        return Value::from_number(args.empty() ? 0 : args[0].to_number());
    }));
    m_globals->variables["isNaN"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        return Value::from_boolean(args.empty() || std::isnan(args[0].to_number()));
    }));

    auto object_constructor = make_native([](std::vector<Value> const& args) {
        return args.empty() ? Value::from_object(std::make_shared<Object>()) : args[0];
    });
    object_constructor->properties["keys"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        auto result = std::make_shared<Object>();
        result->is_array = true;
        if (!args.empty() && args[0].is_object() && args[0].object)
            for (auto const& [key, value] : args[0].object->properties) result->elements.push_back(Value::from_string(key));
        return Value::from_object(result);
    }));
    object_constructor->properties["prototype"] = Value::from_object(m_object_prototype);
    m_globals->variables["Object"] = Value::from_object(object_constructor);

    auto array_constructor = make_native([](std::vector<Value> const&) {
        auto array = std::make_shared<Object>();
        array->is_array = true;
        return Value::from_object(array);
    });
    array_constructor->properties["isArray"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        return Value::from_boolean(!args.empty() && args[0].is_object() && args[0].object && args[0].object->is_array);
    }));
    m_globals->variables["Array"] = Value::from_object(array_constructor);

    // JSON
    auto json = std::make_shared<Object>();
    json->properties["stringify"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        std::string out;
        json_write(args.empty() ? Value::undefined() : args[0], out);
        return Value::from_string(out);
    }));
    json->properties["parse"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        if (args.empty()) return Value::undefined();
        std::string text = args[0].to_string();
        JsonParser parser { text, 0 };
        auto value = parser.parse();
        return parser.ok ? value : Value::undefined();
    }));
    m_globals->variables["JSON"] = Value::from_object(json);

    // RegExp
    m_globals->variables["RegExp"] = Value::from_object(make_native([](std::vector<Value> const& args) {
        std::string pattern = args.empty() ? std::string() : args[0].to_string();
        std::string flags = args.size() >= 2 ? args[1].to_string() : std::string();
        std::string regex_error;
        return make_regex_object(Regex::compile(pattern, flags, regex_error));
    }));

    // Promise
    auto promise_constructor = make_native([this](std::vector<Value> const& args) {
        auto promise = make_promise();
        if (!args.empty() && args[0].is_object() && args[0].object && args[0].object->is_function) {
            auto resolve = make_native([this, p = promise.object](std::vector<Value> const& a) {
                settle_promise(p, false, a.empty() ? Value::undefined() : a[0]);
                return Value::undefined();
            });
            auto reject = make_native([this, p = promise.object](std::vector<Value> const& a) {
                settle_promise(p, true, a.empty() ? Value::undefined() : a[0]);
                return Value::undefined();
            });
            call(args[0], { Value::from_object(resolve), Value::from_object(reject) });
        }
        return promise;
    });
    promise_constructor->properties["resolve"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        Value value = args.empty() ? Value::undefined() : args[0];
        if (value.is_object() && value.object && value.object->promise)
            return value;
        auto promise = make_promise();
        settle_promise(promise.object, false, value);
        return promise;
    }));
    promise_constructor->properties["reject"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        auto promise = make_promise();
        settle_promise(promise.object, true, args.empty() ? Value::undefined() : args[0]);
        return promise;
    }));
    promise_constructor->properties["all"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        auto result = make_promise();
        auto array = std::make_shared<Object>();
        array->is_array = true;
        if (!args.empty() && args[0].is_object() && args[0].object && args[0].object->is_array)
            for (auto const& item : args[0].object->elements)
                array->elements.push_back((item.is_object() && item.object && item.object->promise) ? item.object->promise->value : item);
        settle_promise(result.object, false, Value::from_object(array));
        return result;
    }));
    m_globals->variables["Promise"] = Value::from_object(promise_constructor);

    // Date (UTC)
    auto date_constructor = make_native([](std::vector<Value> const& args) {
        double ms = args.empty() ? current_time_ms() : args[0].to_number();
        return make_date_object(ms);
    });
    date_constructor->properties["now"] = Value::from_object(make_native([](std::vector<Value> const&) {
        return Value::from_number(current_time_ms());
    }));
    m_globals->variables["Date"] = Value::from_object(date_constructor);

    // Map
    m_globals->variables["Map"] = Value::from_object(make_native([](std::vector<Value> const&) {
        auto object = std::make_shared<Object>();
        object->is_map = true;
        return Value::from_object(object);
    }));

    // Set
    m_globals->variables["Set"] = Value::from_object(make_native([](std::vector<Value> const&) {
        auto object = std::make_shared<Object>();
        object->is_set = true;
        return Value::from_object(object);
    }));

    auto make_listener_registrar = [this](std::shared_ptr<Object> const& target) {
        return Value::from_object(make_native([target](std::vector<Value> const& args) {
            if (args.size() < 2)
                return Value::undefined();
            std::string key = "__handlers_" + args[0].to_string();
            auto it = target->properties.find(key);
            std::shared_ptr<Object> array;
            if (it != target->properties.end() && it->second.is_object() && it->second.object) {
                array = it->second.object;
            } else {
                array = std::make_shared<Object>();
                array->is_array = true;
                target->properties[key] = Value::from_object(array);
            }
            array->elements.push_back(args[1]);
            return Value::undefined();
        }));
    };

    // window
    m_window = std::make_shared<Object>();
    m_window->properties["addEventListener"] = make_listener_registrar(m_window);
    m_globals->variables["window"] = Value::from_object(m_window);

    // document.write
    auto document = std::make_shared<Object>();
    document->properties["write"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        for (auto const& arg : args)
            if (write_handler)
                write_handler(arg.to_string());
        return Value::undefined();
    }));
    document->properties["writeln"] = document->properties["write"];
    document->properties["getElementById"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        if (args.empty() || !element_lookup)
            return Value::undefined();
        return element_lookup(args[0].to_string());
    }));
    document->properties["createElement"] = Value::from_object(make_native([this](std::vector<Value> const& args) {
        if (args.empty() || !create_element)
            return Value::undefined();
        return create_element(args[0].to_string());
    }));
    m_document = document;
    document->properties["addEventListener"] = make_listener_registrar(document);
    m_globals->variables["document"] = Value::from_object(document);
}

std::shared_ptr<Object> Interpreter::make_array(std::vector<Value> elements)
{
    auto object = std::make_shared<Object>();
    object->is_array = true;
    object->prototype = m_object_prototype;
    object->elements = std::move(elements);
    return object;
}

std::shared_ptr<Object> Interpreter::make_function(Node const& node, std::shared_ptr<Environment> const& env)
{
    auto object = std::make_shared<Object>();
    object->is_function = true;
    object->prototype = m_object_prototype;
    auto prototype = std::make_shared<Object>();
    prototype->prototype = m_object_prototype;
    object->properties["prototype"] = Value::from_object(prototype);
    object->parameters = node.parameters;
    object->is_async = node.boolean;
    object->function_body = std::shared_ptr<Node>(node.body.get(), [](Node*) {});
    object->closure = env;
    return object;
}

static bool same_value(Value const& a, Value const& b)
{
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case Value::Type::Undefined: return true;
    case Value::Type::Null: return true;
    case Value::Type::Boolean: return a.boolean == b.boolean;
    case Value::Type::Number: return a.number == b.number;
    case Value::Type::String: return a.string == b.string;
    case Value::Type::Object: return a.object == b.object;
    }
    return false;
}

Value Interpreter::get_property_from_chain(std::shared_ptr<Object> const& object, std::string const& name)
{
    for (auto current = object; current; current = current->prototype) {
        auto it = current->properties.find(name);
        if (it != current->properties.end())
            return it->second;
    }
    return Value::undefined();
}

Value Interpreter::get_property(Value const& object, std::string const& name)
{
    if (object.is_string()) {
        if (name == "length")
            return Value::from_number(static_cast<double>(object.string.size()));
        std::string receiver = object.string;
        if (name == "toUpperCase")
            return Value::from_object(make_native([receiver](std::vector<Value> const&) {
                std::string result = receiver;
                for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return Value::from_string(result);
            }));
        if (name == "toLowerCase")
            return Value::from_object(make_native([receiver](std::vector<Value> const&) {
                std::string result = receiver;
                for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return Value::from_string(result);
            }));
        if (name == "charAt")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                int index = args.empty() ? 0 : static_cast<int>(args[0].to_number());
                if (index < 0 || index >= static_cast<int>(receiver.size())) return Value::from_string("");
                return Value::from_string(receiver.substr(index, 1));
            }));
        if (name == "indexOf")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_number(-1);
                auto found = receiver.find(args[0].to_string());
                return Value::from_number(found == std::string::npos ? -1 : static_cast<double>(found));
            }));
        if (name == "slice")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                int start = args.empty() ? 0 : static_cast<int>(args[0].to_number());
                int end = args.size() >= 2 ? static_cast<int>(args[1].to_number()) : static_cast<int>(receiver.size());
                if (start < 0) start = static_cast<int>(receiver.size()) + start;
                if (end < 0) end = static_cast<int>(receiver.size()) + end;
                start = std::max(0, start); end = std::min<int>(end, static_cast<int>(receiver.size()));
                if (end < start) end = start;
                return Value::from_string(receiver.substr(start, end - start));
            }));
        if (name == "substring")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                int size = static_cast<int>(receiver.size());
                int start = args.empty() ? 0 : std::max(0, std::min(size, static_cast<int>(args[0].to_number())));
                int end = args.size() >= 2 ? std::max(0, std::min(size, static_cast<int>(args[1].to_number()))) : size;
                if (start > end) std::swap(start, end);
                return Value::from_string(receiver.substr(start, end - start));
            }));
        if (name == "split")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                std::vector<Value> parts;
                if (args.empty()) {
                    parts.push_back(Value::from_string(receiver));
                } else if (args[0].is_object() && args[0].object && args[0].object->regex) {
                    auto const& regex = args[0].object->regex;
                    std::size_t position = 0, start = 0, end = 0;
                    while (position <= receiver.size() && regex->search(receiver, position, start, end)) {
                        parts.push_back(Value::from_string(receiver.substr(position, start - position)));
                        position = end > start ? end : start + 1;
                    }
                    parts.push_back(Value::from_string(receiver.substr(position)));
                } else {
                    std::string separator = args[0].to_string();
                    if (separator.empty()) {
                        for (char c : receiver) parts.push_back(Value::from_string(std::string(1, c)));
                    } else {
                        size_t pos = 0;
                        while (true) {
                            auto next = receiver.find(separator, pos);
                            if (next == std::string::npos) { parts.push_back(Value::from_string(receiver.substr(pos))); break; }
                            parts.push_back(Value::from_string(receiver.substr(pos, next - pos)));
                            pos = next + separator.size();
                        }
                    }
                }
                auto array = std::make_shared<Object>();
                array->is_array = true;
                array->elements = std::move(parts);
                return Value::from_object(array);
            }));
        if (name == "replace")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                if (args.size() < 2) return Value::from_string(receiver);
                std::string replacement = args[1].to_string();
                if (args[0].is_object() && args[0].object && args[0].object->regex) {
                    auto const& regex = args[0].object->regex;
                    std::string result;
                    std::size_t position = 0, start = 0, end = 0;
                    while (position <= receiver.size() && regex->search(receiver, position, start, end)) {
                        result += receiver.substr(position, start - position);
                        result += replacement;
                        position = end > start ? end : start + 1;
                        if (!regex->global())
                            break;
                    }
                    result += receiver.substr(position);
                    return Value::from_string(result);
                }
                std::string search = args[0].to_string();
                auto pos = search.empty() ? std::string::npos : receiver.find(search);
                if (pos == std::string::npos) return Value::from_string(receiver);
                return Value::from_string(receiver.substr(0, pos) + replacement + receiver.substr(pos + search.size()));
            }));
        if (name == "search")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                if (args.empty() || !args[0].is_object() || !args[0].object || !args[0].object->regex)
                    return Value::from_number(-1);
                std::size_t start = 0, end = 0;
                return Value::from_number(args[0].object->regex->search(receiver, 0, start, end) ? static_cast<double>(start) : -1);
            }));
        if (name == "match")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                if (args.empty() || !args[0].is_object() || !args[0].object || !args[0].object->regex)
                    return Value::null();
                auto const& regex = args[0].object->regex;
                auto array = std::make_shared<Object>();
                array->is_array = true;
                std::size_t position = 0, start = 0, end = 0;
                while (position <= receiver.size() && regex->search(receiver, position, start, end)) {
                    array->elements.push_back(Value::from_string(receiver.substr(start, end - start)));
                    if (!regex->global()) {
                        array->properties["index"] = Value::from_number(static_cast<double>(start));
                        break;
                    }
                    position = end > start ? end : start + 1;
                }
                if (array->elements.empty())
                    return Value::null();
                return Value::from_object(array);
            }));
        if (name == "repeat")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                int count = args.empty() ? 0 : std::max(0, static_cast<int>(args[0].to_number()));
                std::string result;
                for (int i = 0; i < count; ++i) result += receiver;
                return Value::from_string(result);
            }));
        if (name == "trim")
            return Value::from_object(make_native([receiver](std::vector<Value> const&) {
                size_t begin = receiver.find_first_not_of(" \t\r\n");
                size_t end = receiver.find_last_not_of(" \t\r\n");
                return Value::from_string(begin == std::string::npos ? std::string() : receiver.substr(begin, end - begin + 1));
            }));
        if (name == "includes")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                return Value::from_boolean(!args.empty() && receiver.find(args[0].to_string()) != std::string::npos);
            }));
        if (name == "startsWith")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                return Value::from_boolean(!args.empty() && receiver.rfind(args[0].to_string(), 0) == 0);
            }));
        if (name == "endsWith")
            return Value::from_object(make_native([receiver](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                std::string suffix = args[0].to_string();
                return Value::from_boolean(receiver.size() >= suffix.size() && receiver.compare(receiver.size() - suffix.size(), suffix.size(), suffix) == 0);
            }));
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            unsigned long index = std::strtoul(name.c_str(), nullptr, 10);
            if (index < receiver.size())
                return Value::from_string(receiver.substr(index, 1));
            return Value::undefined();
        }
        return Value::undefined();
    }

    if (object.is_number()) {
        if (name == "toString")
            return Value::from_object(make_native([object](std::vector<Value> const&) { return Value::from_string(object.to_string()); }));
        if (name == "toFixed")
            return Value::from_object(make_native([object](std::vector<Value> const& args) {
                int digits = args.empty() ? 0 : static_cast<int>(args[0].to_number());
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%.*f", std::max(0, std::min(20, digits)), object.number);
                return Value::from_string(buffer);
            }));
        return Value::undefined();
    }

    if (!object.is_object() || !object.object)
        return Value::undefined();

    auto const& shared = object.object;
    if (shared->is_array) {
        if (name == "length")
            return Value::from_number(static_cast<double>(shared->elements.size()));
        if (name == "push")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                for (auto const& arg : args) shared->elements.push_back(arg);
                return Value::from_number(static_cast<double>(shared->elements.size()));
            }));
        if (name == "pop")
            return Value::from_object(make_native([shared](std::vector<Value> const&) {
                if (shared->elements.empty()) return Value::undefined();
                auto value = shared->elements.back();
                shared->elements.pop_back();
                return value;
            }));
        if (name == "join")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                std::string separator = args.empty() ? "," : args[0].to_string();
                std::string result;
                for (size_t i = 0; i < shared->elements.size(); ++i) {
                    if (i) result += separator;
                    result += shared->elements[i].to_string();
                }
                return Value::from_string(result);
            }));
        if (name == "indexOf")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_number(-1);
                for (size_t i = 0; i < shared->elements.size(); ++i)
                    if (shared->elements[i].strict_equals(args[0]))
                        return Value::from_number(static_cast<double>(i));
                return Value::from_number(-1);
            }));
        if (name == "slice")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                int start = args.empty() ? 0 : static_cast<int>(args[0].to_number());
                int end = args.size() >= 2 ? static_cast<int>(args[1].to_number()) : static_cast<int>(shared->elements.size());
                int size = static_cast<int>(shared->elements.size());
                if (start < 0) start = size + start;
                if (end < 0) end = size + end;
                start = std::max(0, start); end = std::min(end, size);
                std::vector<Value> result;
                for (int i = start; i < end; ++i) result.push_back(shared->elements[i]);
                auto array = std::make_shared<Object>();
                array->is_array = true;
                array->elements = std::move(result);
                return Value::from_object(array);
            }));
        if (name == "forEach" || name == "map" || name == "filter") {
            return Value::from_object(make_native([this, shared, name](std::vector<Value> const& args) {
                auto result = std::make_shared<Object>();
                result->is_array = true;
                if (args.empty()) return Value::from_object(result);
                Value callback = args[0];
                for (size_t i = 0; i < shared->elements.size(); ++i) {
                    std::vector<Value> callback_args { shared->elements[i], Value::from_number(static_cast<double>(i)) };
                    auto value = call(callback, callback_args);
                    if (name == "forEach") continue;
                    if (name == "map") result->elements.push_back(value);
                    else if (value.to_boolean()) result->elements.push_back(shared->elements[i]);
                }
                return Value::from_object(result);
            }));
        }
        if (name == "reduce")
            return Value::from_object(make_native([this, shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::undefined();
                Value callback = args[0];
                Value accumulator = args.size() >= 2 ? args[1] : Value::undefined();
                size_t start = 0;
                if (args.size() < 2) {
                    if (shared->elements.empty()) return Value::undefined();
                    accumulator = shared->elements[0];
                    start = 1;
                }
                for (size_t i = start; i < shared->elements.size(); ++i) {
                    std::vector<Value> callback_args { accumulator, shared->elements[i], Value::from_number(static_cast<double>(i)) };
                    accumulator = call(callback, callback_args);
                }
                return accumulator;
            }));
        if (name == "reverse")
            return Value::from_object(make_native([shared](std::vector<Value> const&) {
                std::reverse(shared->elements.begin(), shared->elements.end());
                return Value::from_object(shared);
            }));
        if (name == "sort")
            return Value::from_object(make_native([shared](std::vector<Value> const&) {
                std::sort(shared->elements.begin(), shared->elements.end(), [](Value const& a, Value const& b) { return a.to_string() < b.to_string(); });
                return Value::from_object(shared);
            }));
        if (name == "includes")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                for (auto const& element : shared->elements)
                    if (same_value(element, args[0])) return Value::from_boolean(true);
                return Value::from_boolean(false);
            }));
        if (name == "concat")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                auto result = std::make_shared<Object>();
                result->is_array = true;
                result->elements = shared->elements;
                for (auto const& arg : args) {
                    if (arg.is_object() && arg.object && arg.object->is_array)
                        for (auto const& element : arg.object->elements) result->elements.push_back(element);
                    else
                        result->elements.push_back(arg);
                }
                return Value::from_object(result);
            }));
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            unsigned long index = std::strtoul(name.c_str(), nullptr, 10);
            if (index < shared->elements.size())
                return shared->elements[index];
            return Value::undefined();
        }
        return get_property_from_chain(shared, name);
    }

    if (shared->is_map) {
        if (name == "size") return Value::from_number(static_cast<double>(shared->entries.size()));
        if (name == "set")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.size() >= 2) {
                    for (auto& entry : shared->entries)
                        if (same_value(entry.first, args[0])) { entry.second = args[1]; return Value::from_object(shared); }
                    shared->entries.emplace_back(args[0], args[1]);
                }
                return Value::from_object(shared);
            }));
        if (name == "get")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::undefined();
                for (auto const& entry : shared->entries)
                    if (same_value(entry.first, args[0])) return entry.second;
                return Value::undefined();
            }));
        if (name == "has")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                for (auto const& entry : shared->entries)
                    if (same_value(entry.first, args[0])) return Value::from_boolean(true);
                return Value::from_boolean(false);
            }));
        if (name == "delete")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                for (auto it = shared->entries.begin(); it != shared->entries.end(); ++it)
                    if (same_value(it->first, args[0])) { shared->entries.erase(it); return Value::from_boolean(true); }
                return Value::from_boolean(false);
            }));
        if (name == "keys")
            return Value::from_object(make_native([shared](std::vector<Value> const&) {
                auto array = std::make_shared<Object>();
                array->is_array = true;
                for (auto const& entry : shared->entries) array->elements.push_back(entry.first);
                return Value::from_object(array);
            }));
        if (name == "values")
            return Value::from_object(make_native([shared](std::vector<Value> const&) {
                auto array = std::make_shared<Object>();
                array->is_array = true;
                for (auto const& entry : shared->entries) array->elements.push_back(entry.second);
                return Value::from_object(array);
            }));
        return get_property_from_chain(shared, name);
    }

    if (shared->is_set) {
        if (name == "size") return Value::from_number(static_cast<double>(shared->elements.size()));
        if (name == "add")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (!args.empty()) {
                    bool found = false;
                    for (auto const& element : shared->elements)
                        if (same_value(element, args[0])) found = true;
                    if (!found) shared->elements.push_back(args[0]);
                }
                return Value::from_object(shared);
            }));
        if (name == "has")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                for (auto const& element : shared->elements)
                    if (same_value(element, args[0])) return Value::from_boolean(true);
                return Value::from_boolean(false);
            }));
        if (name == "delete")
            return Value::from_object(make_native([shared](std::vector<Value> const& args) {
                if (args.empty()) return Value::from_boolean(false);
                for (auto it = shared->elements.begin(); it != shared->elements.end(); ++it)
                    if (same_value(*it, args[0])) { shared->elements.erase(it); return Value::from_boolean(true); }
                return Value::from_boolean(false);
            }));
        return get_property_from_chain(shared, name);
    }

    return get_property_from_chain(shared, name);
}

void Interpreter::set_property(Value const& object, std::string const& name, Value value)
{
    if (!object.is_object() || !object.object)
        return;
    if (object.object->is_array) {
        char* end = nullptr;
        long index = std::strtol(name.c_str(), &end, 10);
        if (end != name.c_str() && *end == '\0' && index >= 0) {
            if (static_cast<size_t>(index) >= object.object->elements.size())
                object.object->elements.resize(index + 1);
            object.object->elements[index] = std::move(value);
            return;
        }
    }
    object.object->properties[name] = std::move(value);
}

Value Interpreter::evaluate_binary(std::string const& op, Value const& left, Value const& right)
{
    if (op == "+") {
        if (left.is_string() || right.is_string())
            return Value::from_string(left.to_string() + right.to_string());
        return Value::from_number(left.to_number() + right.to_number());
    }
    if (op == "-") return Value::from_number(left.to_number() - right.to_number());
    if (op == "*") return Value::from_number(left.to_number() * right.to_number());
    if (op == "/") return Value::from_number(left.to_number() / right.to_number());
    if (op == "%") return Value::from_number(std::fmod(left.to_number(), right.to_number()));
    if (op == "===") return Value::from_boolean(left.strict_equals(right));
    if (op == "!==") return Value::from_boolean(!left.strict_equals(right));
    if (op == "==" || op == "!=") {
        bool equal = left.strict_equals(right);
        if (!equal && (left.is_number() || left.is_string() || left.is_boolean()) && (right.is_number() || right.is_string() || right.is_boolean()))
            equal = left.to_number() == right.to_number() || left.to_string() == right.to_string();
        return Value::from_boolean(op == "==" ? equal : !equal);
    }
    if (op == "<") return Value::from_boolean(left.to_number() < right.to_number());
    if (op == ">") return Value::from_boolean(left.to_number() > right.to_number());
    if (op == "<=") return Value::from_boolean(left.to_number() <= right.to_number());
    if (op == ">=") return Value::from_boolean(left.to_number() >= right.to_number());
    if (op == "instanceof") {
        if (!left.is_object() || !left.object || !right.is_object() || !right.object)
            return Value::from_boolean(false);
        auto target = get_property(right, "prototype");
        if (!target.is_object() || !target.object)
            return Value::from_boolean(false);
        for (auto current = left.object; current; current = current->prototype)
            if (current == target.object)
                return Value::from_boolean(true);
        return Value::from_boolean(false);
    }
    return Value::undefined();
}

Value Interpreter::call_function(std::shared_ptr<Object> const& function, std::vector<Value> const& arguments, Value const& this_value)
{
    if (function->is_native)
        return function->native(arguments);

    auto env = std::make_shared<Environment>();
    env->parent = function->closure ? function->closure : m_globals;
    env->variables["this"] = this_value;
    for (size_t i = 0; i < function->parameters.size(); ++i)
        env->variables[function->parameters[i]] = i < arguments.size() ? arguments[i] : Value::undefined();

    if (function->function_body) {
        auto completion = execute(*function->function_body, env);
        if (function->is_async) {
            auto promise = make_promise();
            settle_promise(promise.object, false, completion.type == Completion::Type::Return ? completion.value : Value::undefined());
            return promise;
        }
        if (completion.type == Completion::Type::Return)
            return completion.value;
    }
    if (function->is_async) {
        auto promise = make_promise();
        settle_promise(promise.object, false, Value::undefined());
        return promise;
    }
    return Value::undefined();
}

Value Interpreter::call(Value const& callee, std::vector<Value> const& arguments, Value const& this_value)
{
    if (callee.is_object() && callee.object && callee.object->is_function)
        return call_function(callee.object, arguments, this_value);
    error("value is not a function");
    return Value::undefined();
}

Value Interpreter::evaluate(Node const& node, std::shared_ptr<Environment> const& env)
{
    if (m_has_error)
        return Value::undefined();

    switch (node.type) {
    case NodeType::NumberLiteral: return Value::from_number(node.number);
    case NodeType::StringLiteral: return Value::from_string(node.string);
    case NodeType::BooleanLiteral: return Value::from_boolean(node.boolean);
    case NodeType::NullLiteral: return Value::null();
    case NodeType::Identifier: {
        if (auto* found = env->find(node.string))
            return *found;
        error("undefined variable '" + node.string + "'");
        return Value::undefined();
    }
    case NodeType::ArrayLiteral: {
        std::vector<Value> elements;
        for (auto const& child : node.children)
            elements.push_back(evaluate(*child, env));
        return Value::from_object(make_array(std::move(elements)));
    }
    case NodeType::ObjectLiteral: {
        auto object = std::make_shared<Object>();
        object->prototype = m_object_prototype;
        for (auto const& [key, value] : node.properties)
            object->properties[key] = evaluate(*value, env);
        return Value::from_object(object);
    }
    case NodeType::FunctionExpression:
    case NodeType::FunctionDeclaration:
        return Value::from_object(make_function(node, env));
    case NodeType::ClassDeclaration: {
        auto constructor = node.left ? make_function(*node.left, env) : make_function(Node {}, env);
        auto prototype_value = constructor->properties["prototype"];
        if (prototype_value.is_object() && prototype_value.object)
            for (auto const& [name, function_node] : node.properties)
                prototype_value.object->properties[name] = Value::from_object(make_function(*function_node, env));
        return Value::from_object(constructor);
    }
    case NodeType::Unary: {
        if (node.string == "await") {
            auto value = evaluate(*node.left, env);
            drain_microtasks();
            if (value.is_object() && value.object && value.object->promise) {
                if (value.object->promise->status == PromiseData::Status::Rejected)
                    error("unhandled promise rejection");
                return value.object->promise->value;
            }
            return value;
        }
        if (node.string == "!") return Value::from_boolean(!evaluate(*node.left, env).to_boolean());
        if (node.string == "-") return Value::from_number(-evaluate(*node.left, env).to_number());
        if (node.string == "+") return Value::from_number(evaluate(*node.left, env).to_number());
        if (node.string == "typeof") {
            auto value = evaluate(*node.left, env);
            switch (value.type) {
            case Value::Type::Undefined: return Value::from_string("undefined");
            case Value::Type::Null: return Value::from_string("object");
            case Value::Type::Boolean: return Value::from_string("boolean");
            case Value::Type::Number: return Value::from_string("number");
            case Value::Type::String: return Value::from_string("string");
            case Value::Type::Object: return Value::from_string(value.object && value.object->is_function ? "function" : "object");
            }
        }
        if (node.string == "++" || node.string == "--") {
            // Support the common `i++` / `++i` on an identifier.
            if (node.left->type == NodeType::Identifier) {
                auto* slot = env->find(node.left->string);
                if (!slot) { error("undefined variable '" + node.left->string + "'"); return Value::undefined(); }
                double previous = slot->to_number();
                slot->number = previous + (node.string == "++" ? 1 : -1);
                slot->type = Value::Type::Number;
                return Value::from_number(previous);
            }
        }
        return Value::undefined();
    }
    case NodeType::Binary: {
        auto left = evaluate(*node.left, env);
        auto right = evaluate(*node.right, env);
        return evaluate_binary(node.string, left, right);
    }
    case NodeType::Logical: {
        auto left = evaluate(*node.left, env);
        if (node.string == "&&")
            return left.to_boolean() ? evaluate(*node.right, env) : left;
        return left.to_boolean() ? left : evaluate(*node.right, env);
    }
    case NodeType::Conditional: {
        return evaluate(*node.left, env).to_boolean() ? evaluate(*node.right, env) : evaluate(*node.third, env);
    }
    case NodeType::Assignment: {
        auto value = evaluate(*node.right, env);
        if (node.string != "=") {
            auto current = evaluate(*node.left, env);
            value = evaluate_binary(std::string(1, node.string[0]), current, value);
        }
        if (node.left->type == NodeType::Identifier) {
            if (auto* slot = env->find(node.left->string))
                *slot = value;
            else
                env->variables[node.left->string] = value;
        } else if (node.left->type == NodeType::Member) {
            auto object = evaluate(*node.left->left, env);
            std::string name = node.left->right ? evaluate(*node.left->right, env).to_string() : node.left->string;
            set_property(object, name, value);
        }
        return value;
    }
    case NodeType::Member: {
        auto object = evaluate(*node.left, env);
        std::string name = node.right ? evaluate(*node.right, env).to_string() : node.string;
        return get_property(object, name);
    }
    case NodeType::RegexLiteral:
        return make_regex_object(node.regex);
    case NodeType::Call: {
        Value this_value;
        Value callee;
        if (node.left->type == NodeType::Member) {
            this_value = evaluate(*node.left->left, env);
            std::string name = node.left->right ? evaluate(*node.left->right, env).to_string() : node.left->string;
            callee = get_property(this_value, name);
        } else {
            callee = evaluate(*node.left, env);
        }
        std::vector<Value> arguments;
        for (auto const& child : node.children)
            arguments.push_back(evaluate(*child, env));
        return call(callee, arguments, this_value);
    }
    case NodeType::New: {
        auto constructor = evaluate(*node.left, env);
        std::vector<Value> arguments;
        for (auto const& child : node.children)
            arguments.push_back(evaluate(*child, env));
        auto instance = std::make_shared<Object>();
        auto prototype_property = get_property(constructor, "prototype");
        instance->prototype = (prototype_property.is_object() && prototype_property.object) ? prototype_property.object : m_object_prototype;
        Value this_value = Value::from_object(instance);
        auto result = call(constructor, arguments, this_value);
        if (result.is_object() && result.object)
            return result;
        return this_value;
    }
    default:
        return Value::undefined();
    }
}

Interpreter::Completion Interpreter::execute(Node const& node, std::shared_ptr<Environment> const& env)
{
    Completion completion;
    if (m_has_error)
        return completion;

    switch (node.type) {
    case NodeType::Program:
    case NodeType::Block: {
        auto block_env = node.type == NodeType::Block ? std::make_shared<Environment>() : env;
        if (node.type == NodeType::Block)
            block_env->parent = env;
        for (auto const& child : node.children) {
            completion = execute(*child, block_env);
            if (completion.type != Completion::Type::Normal || m_has_error)
                return completion;
        }
        return completion;
    }
    case NodeType::ExpressionStatement:
        completion.value = evaluate(*node.left, env);
        return completion;
    case NodeType::VariableDeclaration: {
        for (size_t i = 0; i < node.names.size(); ++i) {
            Value value = (i < node.initializers.size() && node.initializers[i]) ? evaluate(*node.initializers[i], env) : Value::undefined();
            env->variables[node.names[i]] = std::move(value);
        }
        return completion;
    }
    case NodeType::FunctionDeclaration: {
        auto function = evaluate(node, env);
        env->variables[node.string] = std::move(function);
        return completion;
    }
    case NodeType::ClassDeclaration: {
        auto klass = evaluate(node, env);
        env->variables[node.string] = std::move(klass);
        return completion;
    }
    case NodeType::IfStatement: {
        if (evaluate(*node.test, env).to_boolean())
            return execute(*node.body, env);
        if (node.alternate)
            return execute(*node.alternate, env);
        return completion;
    }
    case NodeType::WhileStatement: {
        while (evaluate(*node.test, env).to_boolean() && !m_has_error) {
            auto result = execute(*node.body, env);
            if (result.type == Completion::Type::Return) return result;
            if (result.type == Completion::Type::Break) break;
        }
        return completion;
    }
    case NodeType::ForStatement: {
        auto loop_env = std::make_shared<Environment>();
        loop_env->parent = env;
        if (node.init) {
            auto result = execute(*node.init, loop_env);
            if (result.type != Completion::Type::Normal) return result;
        }
        while (!node.test || evaluate(*node.test, loop_env).to_boolean()) {
            if (m_has_error) break;
            if (node.body) {
                auto result = execute(*node.body, loop_env);
                if (result.type == Completion::Type::Return) return result;
                if (result.type == Completion::Type::Break) break;
            }
            if (node.update) evaluate(*node.update, loop_env);
        }
        return completion;
    }
    case NodeType::ForOfStatement: {
        auto iterable = evaluate(*node.right, env);
        std::vector<Value> items;
        if (iterable.is_object() && iterable.object && iterable.object->is_array)
            items = iterable.object->elements;
        else if (iterable.is_string())
            for (char c : iterable.string) items.push_back(Value::from_string(std::string(1, c)));
        else { error("value is not iterable"); return completion; }
        for (auto& item : items) {
            env->variables[node.string] = item;
            auto result = execute(*node.body, env);
            if (result.type == Completion::Type::Return) return result;
            if (result.type == Completion::Type::Break) break;
        }
        return completion;
    }
    case NodeType::SwitchStatement: {
        auto discriminant = evaluate(*node.test, env);
        auto switch_env = std::make_shared<Environment>();
        switch_env->parent = env;
        int start = -1;
        for (size_t i = 0; i < node.children.size(); ++i)
            if (node.initializers[i] && evaluate(*node.initializers[i], env).strict_equals(discriminant)) { start = static_cast<int>(i); break; }
        if (start < 0)
            for (size_t i = 0; i < node.children.size(); ++i)
                if (!node.initializers[i]) { start = static_cast<int>(i); break; }
        if (start < 0) return completion;
        for (size_t i = start; i < node.children.size(); ++i) {
            auto result = execute(*node.children[i], switch_env);
            if (result.type == Completion::Type::Return) return result;
            if (result.type == Completion::Type::Break) return completion;
        }
        return completion;
    }
    case NodeType::ReturnStatement:
        completion.type = Completion::Type::Return;
        completion.value = node.left ? evaluate(*node.left, env) : Value::undefined();
        return completion;
    case NodeType::BreakStatement:
        completion.type = Completion::Type::Break;
        return completion;
    case NodeType::ContinueStatement:
        completion.type = Completion::Type::Continue;
        return completion;
    default:
        completion.value = evaluate(node, env);
        return completion;
    }
}

void Interpreter::dispatch_event_to(Value const& target, std::string const& type)
{
    if (!target.is_object() || !target.object)
        return;
    auto object = target.object;
    auto on_property = object->properties.find("on" + type);
    if (on_property != object->properties.end())
        call(on_property->second, {}, target);
    auto handlers = object->properties.find("__handlers_" + type);
    if (handlers != object->properties.end() && handlers->second.is_object() && handlers->second.object && handlers->second.object->is_array)
        for (auto const& handler : handlers->second.object->elements)
            call(handler, {}, target);
}

void Interpreter::dispatch_event(std::string const& type)
{
    dispatch_event_to(Value::from_object(m_document), type);
    dispatch_event_to(Value::from_object(m_window), type);
    drain_microtasks();
}

Value Interpreter::run(std::string const& source)
{
    m_has_error = false;
    m_error.clear();

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    std::shared_ptr<Node> program(parser.parse_program());
    if (parser.failed()) {
        error("parse error: " + parser.error());
        return Value::undefined();
    }
    m_programs.push_back(program);
    auto result = execute(*program, m_globals).value;
    drain_microtasks();
    return result;
}

} // namespace aetheris::js
