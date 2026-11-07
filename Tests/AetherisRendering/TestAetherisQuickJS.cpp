// Verifies the vendored QuickJS engine builds and runs in-tree, covering the
// language features the hand-written interpreter only approximated: arrow
// functions, template literals, classes, regex, destructuring, correct
// ECMAScript number formatting, and async via the pending-job queue.

#include "quickjs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static std::string g_output;

static JSValue js_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    for (int i = 0; i < argc; ++i) {
        const char* text = JS_ToCString(ctx, argv[i]);
        if (i)
            g_output += ' ';
        g_output += text ? text : "undefined";
        JS_FreeCString(ctx, text);
    }
    g_output += '\n';
    return JS_UNDEFINED;
}

static void drain_jobs(JSRuntime* runtime)
{
    for (int guard = 0; guard < 1000; ++guard) {
        JSContext* job_context = nullptr;
        if (JS_ExecutePendingJob(runtime, &job_context) <= 0)
            break;
    }
}

int main()
{
    JSRuntime* runtime = JS_NewRuntime();
    require(runtime != nullptr, "runtime");
    JSContext* context = JS_NewContext(runtime);
    require(context != nullptr, "context");

    JSValue global = JS_GetGlobalObject(context);
    JSValue console = JS_NewObject(context);
    JS_SetPropertyStr(context, console, "log", JS_NewCFunction(context, js_log, "log", 1));
    JS_SetPropertyStr(context, global, "console", console);
    JS_FreeValue(context, global);

    const char* source = R"JS(
        console.log('arrow', [1,2,3].map(x => x * 2).join(','));
        console.log('template', `sum=${1 + 1}`);
        class Greeter { constructor(n) { this.n = n; } hi() { return 'hi ' + this.n; } }
        console.log('class', new Greeter('qjs').hi());
        console.log('regex', 'a1b22'.match(/[0-9]+/g).join('|'));
        console.log('number', 0.1 + 0.2);
        console.log('destructure', (() => { const [a, b] = [7, 8]; return a + b; })());
        async function answer() { return await Promise.resolve(42); }
        answer().then(value => console.log('async', value));
    )JS";

    JSValue result = JS_Eval(context, source, std::strlen(source), "test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(context);
        const char* message = JS_ToCString(context, exception);
        std::fprintf(stderr, "javascript error: %s\n", message ? message : "?");
        JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
        require(false, "script evaluates");
    }
    JS_FreeValue(context, result);
    drain_jobs(runtime);

    require(g_output == "arrow 2,4,6\ntemplate sum=2\nclass hi qjs\nregex 1|22\n"
                        "number 0.30000000000000004\ndestructure 15\nasync 42\n",
        "quickjs output");

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return 0;
}
