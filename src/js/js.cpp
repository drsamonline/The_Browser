#include "js.hpp"

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
        "break", "continue", "true", "false", "null", "undefined", "new", "typeof", "else", "instanceof"
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
        auto statement = make_node(NodeType::ForStatement);
        expect("(");
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
        node->left = parse_postfix();
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

std::unique_ptr<Node> Parser::parse_postfix()
{
    auto expression = parse_primary();
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

std::unique_ptr<Node> Parser::parse_primary()
{
    Token const& token = peek();

    if (token.type == TokenType::Number) {
        advance();
        auto node = make_node(NodeType::NumberLiteral);
        node->number = token.number;
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
                std::string search = args[0].to_string();
                std::string replacement = args[1].to_string();
                auto pos = search.empty() ? std::string::npos : receiver.find(search);
                if (pos == std::string::npos) return Value::from_string(receiver);
                return Value::from_string(receiver.substr(0, pos) + replacement + receiver.substr(pos + search.size()));
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
        if (completion.type == Completion::Type::Return)
            return completion.value;
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
    case NodeType::FunctionDeclaration: {
        auto object = std::make_shared<Object>();
        object->is_function = true;
        object->prototype = m_object_prototype;
        {
            auto prototype = std::make_shared<Object>();
            prototype->prototype = m_object_prototype;
            object->properties["prototype"] = Value::from_object(prototype);
        }
        object->parameters = node.parameters;
        object->function_body = std::make_shared<Node>();
        if (node.body) {
            // Deep-copy the body by sharing it; nodes outlive the call because
            // the whole AST is owned by the caller of run().
            object->function_body = std::shared_ptr<Node>(node.body.get(), [](Node*) {});
        }
        object->closure = env;
        return Value::from_object(object);
    }
    case NodeType::Unary: {
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
        Value constructor;
        std::vector<Value> arguments;
        if (node.left && node.left->type == NodeType::Call) {
            constructor = evaluate(*node.left->left, env);
            for (auto const& child : node.left->children)
                arguments.push_back(evaluate(*child, env));
        } else {
            constructor = evaluate(*node.left, env);
        }
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

Value Interpreter::run(std::string const& source)
{
    m_has_error = false;
    m_error.clear();

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto program = parser.parse_program();
    if (parser.failed()) {
        error("parse error: " + parser.error());
        return Value::undefined();
    }
    return execute(*program, m_globals).value;
}

} // namespace aetheris::js
