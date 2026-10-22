#pragma once

// AETHERIS MINI JAVASCRIPT ENGINE
//
// A small, dependency-free JavaScript interpreter for the Aetheris runtime.
// It is deliberately a subset of the language: enough to run the kind of
// scripts found on simple pages (variables, control flow, functions, arrays,
// objects, and a few built-ins), not a conforming implementation. The full
// LibJS engine in the tree is a bytecode VM that needs a Rust toolchain, a
// clang GC plugin and ICU, so it cannot be built in the standalone runtime;
// this engine is the self-contained alternative.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aetheris::js {

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

struct Object;
struct Node;
struct Environment;

struct Value {
    enum class Type : uint8_t { Undefined, Null, Boolean, Number, String, Object };

    Type type { Type::Undefined };
    bool boolean { false };
    double number { 0 };
    std::string string;
    std::shared_ptr<Object> object;

    static Value undefined() { return {}; }
    static Value null() { Value v; v.type = Type::Null; return v; }
    static Value from_boolean(bool b) { Value v; v.type = Type::Boolean; v.boolean = b; return v; }
    static Value from_number(double n) { Value v; v.type = Type::Number; v.number = n; return v; }
    static Value from_string(std::string s) { Value v; v.type = Type::String; v.string = std::move(s); return v; }
    static Value from_object(std::shared_ptr<Object> o) { Value v; v.type = Type::Object; v.object = std::move(o); return v; }

    bool is_undefined() const { return type == Type::Undefined; }
    bool is_null() const { return type == Type::Null; }
    bool is_boolean() const { return type == Type::Boolean; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_object() const { return type == Type::Object; }

    bool to_boolean() const;
    double to_number() const;
    std::string to_string() const;
    bool strict_equals(Value const& other) const;
};

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------

enum class NodeType : uint8_t {
    NumberLiteral, StringLiteral, BooleanLiteral, NullLiteral, Identifier,
    ArrayLiteral, ObjectLiteral, FunctionExpression,
    Unary, Binary, Logical, Assignment, Conditional, Call, Member, New,
    // statements
    Program, ExpressionStatement, VariableDeclaration, Block,
    IfStatement, WhileStatement, ForStatement, ForOfStatement, SwitchStatement, ReturnStatement,
    BreakStatement, ContinueStatement, FunctionDeclaration, ClassDeclaration,
};

struct Node {
    NodeType type { NodeType::Program };
    double number { 0 };
    std::string string;   // identifier / operator / string value
    bool boolean { false };

    std::vector<std::unique_ptr<Node>> children;          // operands / body
    std::vector<std::pair<std::string, std::unique_ptr<Node>>> properties; // object literal
    std::vector<std::string> names;                       // variable declaration names
    std::vector<std::unique_ptr<Node>> initializers;      // variable declaration inits
    std::vector<std::string> parameters;                  // function parameters

    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    std::unique_ptr<Node> third;
    std::unique_ptr<Node> body;
    std::unique_ptr<Node> alternate;
    std::unique_ptr<Node> init;
    std::unique_ptr<Node> test;
    std::unique_ptr<Node> update;

    bool kind_let { false };  // let/const vs var (cosmetic in this subset)
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

enum class TokenType : uint8_t {
    EndOfInput, Number, String, Identifier, Keyword, Punctuator,
};

struct Token {
    TokenType type { TokenType::EndOfInput };
    std::string text;
    double number { 0 };
};

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> tokenize();

private:
    std::string m_source;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<Node> parse_program();
    std::string const& error() const { return m_error; }
    bool failed() const { return !m_error.empty(); }

private:
    std::unique_ptr<Node> parse_statement();
    std::unique_ptr<Node> parse_block();
    std::unique_ptr<Node> parse_expression();
    std::unique_ptr<Node> parse_assignment();
    std::unique_ptr<Node> parse_conditional();
    std::unique_ptr<Node> parse_binary(int min_precedence);
    std::unique_ptr<Node> parse_unary();
    std::unique_ptr<Node> parse_postfix();
    std::unique_ptr<Node> parse_primary();
    std::unique_ptr<Node> parse_function(bool is_declaration);
    std::unique_ptr<Node> parse_class();

    Token const& peek(int offset = 0) const;
    Token const& advance();
    bool match(char const* text);
    bool check(char const* text) const;
    bool expect(char const* text);
    void set_error(std::string message);

    std::vector<Token> m_tokens;
    size_t m_position { 0 };
    std::string m_error;
};

// ---------------------------------------------------------------------------
// Runtime objects
// ---------------------------------------------------------------------------

using NativeFunction = std::function<Value(std::vector<Value> const&)>;

struct Object {
    std::map<std::string, Value> properties;
    std::vector<Value> elements;
    std::shared_ptr<Object> prototype; // [[Prototype]] chain link
    bool is_array { false };
    bool is_function { false };
    bool is_native { false };

    // User-defined function
    std::vector<std::string> parameters;
    std::shared_ptr<Node> function_body;
    std::shared_ptr<Environment> closure;

    // Native function
    NativeFunction native;
};

struct Environment {
    std::shared_ptr<Environment> parent;
    std::map<std::string, Value> variables;

    Value* find(std::string const& name);
};

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------

class Interpreter {
public:
    Interpreter();

    // Runs a script. Returns the completion value of the last statement.
    Value run(std::string const& source);

    bool has_error() const { return m_has_error; }
    std::string const& error() const { return m_error; }

    // Dispatches an event to the window/document handlers (on<type> and
    // addEventListener) registered by the script.
    void dispatch_event(std::string const& type);

    // Host hooks.
    std::function<void(std::string const&)> print_handler; // console.log
    std::function<void(std::string const&)> write_handler; // document.write
    std::function<Value(std::string const&)> element_lookup; // document.getElementById
    std::function<Value(std::string const&)> create_element; // document.createElement

private:
    struct Completion {
        enum class Type : uint8_t { Normal, Return, Break, Continue };
        Type type { Type::Normal };
        Value value;
    };

    void setup_globals();

    Value evaluate(Node const& node, std::shared_ptr<Environment> const& env);
    Completion execute(Node const& node, std::shared_ptr<Environment> const& env);

    Value evaluate_binary(std::string const& op, Value const& left, Value const& right);
    Value call(Value const& callee, std::vector<Value> const& arguments, Value const& this_value = {});
    Value call_function(std::shared_ptr<Object> const& function, std::vector<Value> const& arguments, Value const& this_value = {});
    std::shared_ptr<Object> make_function(Node const& function, std::shared_ptr<Environment> const& env);
    Value get_property(Value const& object, std::string const& name);
    Value get_property_from_chain(std::shared_ptr<Object> const& object, std::string const& name);
    void set_property(Value const& object, std::string const& name, Value value);
    std::shared_ptr<Object> make_array(std::vector<Value> elements);

    void error(std::string message);
    void declare(std::shared_ptr<Environment> const& env, std::string const& name, Value value);

    std::shared_ptr<Environment> m_globals;
    std::shared_ptr<Object> m_object_prototype;
    std::shared_ptr<Object> m_window;
    std::shared_ptr<Object> m_document;
    // Parsed programs are kept alive so function bodies (which alias the AST)
    // stay valid after run() returns, e.g. for event handlers.
    std::vector<std::shared_ptr<Node>> m_programs;
    bool m_has_error { false };
    std::string m_error;
};

} // namespace aetheris::js
