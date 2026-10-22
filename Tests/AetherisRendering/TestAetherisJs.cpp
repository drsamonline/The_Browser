#include "js.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aetheris::js;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

struct Harness {
    Interpreter interpreter;
    std::string output;
    std::string written;

    Harness()
    {
        interpreter.print_handler = [this](std::string const& line) { output += line + "\n"; };
        interpreter.write_handler = [this](std::string const& text) { written += text; };
    }

    Value run(std::string const& source)
    {
        return interpreter.run(source);
    }

    bool has_error() const { return interpreter.has_error(); }
    std::string const& error() const { return interpreter.error(); }
};

int main()
{
    // Arithmetic and precedence.
    {
        Harness h;
        h.run("var x = 2 + 3 * 4; console.log(x);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "14\n", "arithmetic precedence");
    }

    // Control flow, functions and recursion.
    {
        Harness h;
        h.run("function fact(n) { if (n <= 1) return 1; return n * fact(n - 1); }"
              "var total = 0;"
              "for (var i = 1; i <= 5; i = i + 1) { total = total + i; }"
              "console.log(fact(5)); console.log(total);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "120\n15\n", "recursion and for-loop");
    }

    // While loop and conditionals.
    {
        Harness h;
        h.run("var n = 3; var out = '';"
              "while (n > 0) { if (n % 2 === 0) { out = out + 'e'; } else { out = out + 'o'; } n = n - 1; }"
              "console.log(out);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "oeo\n", "while + if/else");
    }

    // Arrays.
    {
        Harness h;
        h.run("var a = [1, 2, 3]; a.push(4); console.log(a.length); console.log(a.join('-')); console.log(a.indexOf(3));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "4\n1-2-3-4\n2\n", "arrays");
    }

    // Strings and built-ins.
    {
        Harness h;
        h.run("var s = 'Hello'; console.log(s.length); console.log(s.toUpperCase()); console.log(s.charAt(1));"
              "console.log(Math.max(3, 7, 2)); console.log(Math.floor(3.9)); console.log(parseInt('42') + 1);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "5\nHELLO\ne\n7\n3\n43\n", "strings and built-ins");
    }

    // Objects and member assignment.
    {
        Harness h;
        h.run("var o = { name: 'aetheris', count: 1 }; o.count = o.count + 41; console.log(o.name); console.log(o.count);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "aetheris\n42\n", "objects");
    }

    // document.write output is captured.
    {
        Harness h;
        h.run("document.write('<p>'); document.write('hi'); document.write('</p>');");
        require(!h.has_error(), h.error().c_str());
        require(h.written == "<p>hi</p>", "document.write");
    }

    // Ternary and logical operators.
    {
        Harness h;
        h.run("var v = 5; console.log(v > 3 ? 'big' : 'small'); console.log(v > 3 && v < 10);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "big\ntrue\n", "ternary and logical");
    }

    // Parse errors surface as an error rather than crashing.
    {
        Harness h;
        h.run("var = ;");
        require(h.has_error(), "parse error detected");
    }

    return 0;
}
