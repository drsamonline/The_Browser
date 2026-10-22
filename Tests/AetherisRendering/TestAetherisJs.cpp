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

    // String methods.
    {
        Harness h;
        h.run("console.log('a,b,c'.split(',').length);"
              "console.log('a,b,c'.split(',')[1]);"
              "console.log('hello'.replace('l', 'L'));"
              "console.log('abcdef'.substring(1, 4));"
              "console.log('ab'.repeat(3));"
              "console.log('  x  '.trim());"
              "console.log('hello'.includes('ell'));"
              "console.log('hello'.startsWith('he'));"
              "console.log('hello'.endsWith('lo'));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "3\nb\nheLlo\nbcd\nababab\nx\ntrue\ntrue\ntrue\n", "string methods");
    }

    // Array iteration methods with callbacks.
    {
        Harness h;
        h.run("var a = [1, 2, 3, 4];"
              "console.log(a.map(function (x) { return x * 2; }).join(','));"
              "console.log(a.filter(function (x) { return x % 2 === 0; }).join(','));"
              "console.log(a.reduce(function (acc, x) { return acc + x; }, 0));"
              "var total = 0; a.forEach(function (x) { total = total + x; }); console.log(total);"
              "console.log([3, 1, 2].sort().join(','));"
              "console.log([1, 2, 3].reverse().join(','));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "2,4,6,8\n2,4\n10\n10\n1,2,3\n3,2,1\n", "array methods");
    }

    // Object.keys and Array.isArray.
    {
        Harness h;
        h.run("var o = { a: 1, b: 2 }; console.log(Object.keys(o).join(','));"
              "console.log(Array.isArray([1])); console.log(Array.isArray('x'));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "a,b\ntrue\nfalse\n", "Object.keys / Array.isArray");
    }

    // Closures.
    {
        Harness h;
        h.run("function counter() { var n = 0; return function () { n = n + 1; return n; }; }"
              "var c = counter(); c(); c(); console.log(c());");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "3\n", "closures");
    }

    // Constructors, `new` and `this`.
    {
        Harness h;
        h.run("function Point(x, y) { this.x = x; this.y = y; this.sum = function () { return this.x + this.y; }; }"
              "var p = new Point(3, 4); console.log(p.x); console.log(p.y); console.log(p.sum());");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "3\n4\n7\n", "new + this");
    }

    // JSON round-trip.
    {
        Harness h;
        h.run("var o = { a: 1, b: [true, null, 'x'] }; var s = JSON.stringify(o); console.log(s);"
              "var back = JSON.parse(s); console.log(back.b[2]); console.log(back.a);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "{\"a\":1,\"b\":[true,null,\"x\"]}\nx\n1\n", "JSON");
    }

    // Prototypes and instanceof.
    {
        Harness h;
        h.run("function Animal(name) { this.name = name; }"
              "Animal.prototype.speak = function () { return this.name + ' speaks'; };"
              "function Dog(name) { this.name = name; }"
              "Dog.prototype = new Animal();"
              "var d = new Dog('Rex');"
              "console.log(d.speak());"
              "console.log(d instanceof Dog);"
              "console.log(d instanceof Animal);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "Rex speaks\ntrue\ntrue\n", "prototypes + instanceof");
    }

    // Object.prototype extensions are visible to object literals.
    {
        Harness h;
        h.run("Object.prototype.tag = function () { return 'obj'; }; var o = {}; console.log(o.tag());");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "obj\n", "Object.prototype extension");
    }

    // Classes (sugar over the prototype chain).
    {
        Harness h;
        h.run("class Greeter {"
              "  constructor(name) { this.name = name; }"
              "  greet() { return 'Hi ' + this.name; }"
              "}"
              "var g = new Greeter('Aetheris');"
              "console.log(g.greet());"
              "console.log(g instanceof Greeter);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "Hi Aetheris\ntrue\n", "class syntax");
    }

    // Class inheritance via prototype assignment.
    {
        Harness h;
        h.run("class Base { constructor() { this.kind = 'base'; } describe() { return this.kind; } }"
              "class Derived { constructor() { this.kind = 'derived'; } }"
              "Derived.prototype = new Base();"
              "var d = new Derived();"
              "console.log(d.describe());"
              "console.log(d instanceof Base);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "derived\ntrue\n", "class prototype inheritance");
    }

    // for...of over arrays and strings.
    {
        Harness h;
        h.run("var sum = 0; for (var x of [10, 20, 30]) { sum = sum + x; } console.log(sum);"
              "var s = ''; for (var ch of 'abc') { s = s + ch + '-'; } console.log(s);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "60\na-b-c-\n", "for...of");
    }

    // switch statement (with fall-through and default).
    {
        Harness h;
        h.run("function label(n) { switch (n) { case 1: return 'one'; case 2: return 'two'; default: return 'many'; } }"
              "console.log(label(1)); console.log(label(2)); console.log(label(9));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "one\ntwo\nmany\n", "switch");
    }

    // Events: handlers registered with onload / addEventListener fire on dispatch.
    {
        Harness h;
        h.run("window.onload = function () { console.log('loaded'); };"
              "document.addEventListener('DOMContentLoaded', function () { console.log('ready'); });");
        require(!h.has_error(), h.error().c_str());
        require(h.output.empty(), "handlers do not run before dispatch");
        h.interpreter.dispatch_event("DOMContentLoaded");
        h.interpreter.dispatch_event("load");
        require(h.output == "ready\nloaded\n", "event dispatch");
    }

    // Regular expressions: literals, RegExp(), test/match/replace/split/search.
    {
        Harness h;
        h.run("console.log(/[0-9]+/.test('abc123'));"
              "console.log(/^a.*z$/.test('abcz'));"
              "console.log('a1b2c3'.match(/[0-9]/g).join(','));"
              "console.log('hello world'.replace(/o/g, '0'));"
              "console.log('a,b;c'.split(/[,;]/).join('|'));"
              "console.log('abc'.search(/b/));"
              "console.log(new RegExp('[0-9]+').test('x42'));"
              "console.log(/[A-Z]+/i.test('abc'));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "true\ntrue\n1,2,3\nhell0 w0rld\na|b|c\n1\ntrue\ntrue\n", "regex");
    }

    // Promises: microtasks are deferred until the synchronous script finishes.
    {
        Harness h;
        h.run("var log = [];"
              "Promise.resolve(1).then(function (v) { log.push('then' + v); });"
              "log.push('sync');"
              "Promise.reject('e').catch(function (e) { log.push('catch' + e); });"
              "console.log(log.join(','));");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "sync\n", "microtasks are deferred");
    }

    // async/await and promise chaining.
    {
        Harness h;
        h.run("var result = 0;"
              "async function value() { return 7; }"
              "async function run() {"
              "  var a = await value();"
              "  var b = await Promise.resolve(2).then(function (v) { return v * 10; });"
              "  result = a + b;"
              "}"
              "run();"
              "console.log(result);");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "27\n", "async/await and chaining");
    }

    // Map, Set, Date and Array.includes.
    {
        Harness h;
        h.run("var m = new Map(); m.set('a', 1); m.set('b', 2);"
              "console.log(m.get('a'), m.size, m.has('b'));"
              "var s = new Set(); s.add(1); s.add(1); s.add(2);"
              "console.log(s.size, s.has(2));"
              "console.log([1, 2, 3].includes(2));"
              "console.log(new Date(0).toISOString());"
              "console.log(typeof Date.now());");
        require(!h.has_error(), h.error().c_str());
        require(h.output == "1 2 true\n2 true\ntrue\n1970-01-01T00:00:00.000Z\nnumber\n", "Map/Set/Date");
    }

    // Parse errors surface as an error rather than crashing.
    {
        Harness h;
        h.run("var = ;");
        require(h.has_error(), "parse error detected");
    }

    return 0;
}
