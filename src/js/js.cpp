#include "js.hpp"

#include <algorithm>
#include <cstring>

namespace aetheris::js {

namespace {

Interpreter* interpreter_for(JSContext* context)
{
    return static_cast<Interpreter*>(JS_GetContextOpaque(context));
}

std::string to_string(JSContext* context, JSValueConst value)
{
    std::size_t length = 0;
    const char* text = JS_ToCStringLen(context, &length, value);
    std::string result = text ? std::string(text, length) : std::string();
    if (text)
        JS_FreeCString(context, text);
    return result;
}

// console.log(...)
JSValue js_console_log(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter)
        return JS_UNDEFINED;
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i)
            line += ' ';
        line += to_string(context, argv[i]);
    }
    if (interpreter->print_handler)
        interpreter->print_handler(line);
    return JS_UNDEFINED;
}

// document.write(...)
JSValue js_document_write(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter)
        return JS_UNDEFINED;
    for (int i = 0; i < argc; ++i) {
        if (interpreter->write_handler)
            interpreter->write_handler(to_string(context, argv[i]));
    }
    return JS_UNDEFINED;
}

JSValue js_get_element_by_id(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !interpreter->element_lookup)
        return JS_NULL;
    return interpreter->element_lookup(to_string(context, argv[0]));
}

JSValue js_create_element(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !interpreter->create_element)
        return JS_NULL;
    return interpreter->create_element(to_string(context, argv[0]));
}

JSValue js_query_selector(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !interpreter->query_selector)
        return JS_NULL;
    return interpreter->query_selector(to_string(context, argv[0]));
}

JSValue js_query_selector_all(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !interpreter->query_selector_all)
        return JS_NewArray(context);
    return interpreter->query_selector_all(to_string(context, argv[0]));
}

// addEventListener(type, handler) on window/document/elements.
JSValue js_add_event_listener(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    if (argc < 2 || !JS_IsFunction(context, argv[1]))
        return JS_UNDEFINED;
    std::string key = "__handlers_" + to_string(context, argv[0]);
    JSValue existing = JS_GetPropertyStr(context, this_value, key.c_str());
    JSValue list = existing;
    if (!JS_IsArray(list)) {
        JS_FreeValue(context, existing);
        list = JS_NewArray(context);
        JS_SetPropertyStr(context, this_value, key.c_str(), JS_DupValue(context, list));
    }
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    JS_SetPropertyUint32(context, list, length, JS_DupValue(context, argv[1]));
    JS_FreeValue(context, list);
    return JS_UNDEFINED;
}


// ---- timers ---------------------------------------------------------------

JSValue js_set_timeout(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !JS_IsFunction(context, argv[0]))
        return JS_UNDEFINED;
    double delay = 0;
    if (argc > 1)
        JS_ToFloat64(context, &delay, argv[1]);
    return JS_NewInt32(context, interpreter->add_timer(JS_DupValue(context, argv[0]), delay, false));
}

JSValue js_set_interval(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !JS_IsFunction(context, argv[0]))
        return JS_UNDEFINED;
    double delay = 0;
    if (argc > 1)
        JS_ToFloat64(context, &delay, argv[1]);
    return JS_NewInt32(context, interpreter->add_timer(JS_DupValue(context, argv[0]), delay, true));
}

JSValue js_clear_timer(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1)
        return JS_UNDEFINED;
    int32_t id = 0;
    JS_ToInt32(context, &id, argv[0]);
    interpreter->clear_timer(id);
    return JS_UNDEFINED;
}

JSValue js_queue_microtask(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1 || !JS_IsFunction(context, argv[0]))
        return JS_UNDEFINED;
    // A zero-delay timer is close enough: it runs on the next loop turn.
    auto* interpreter = interpreter_for(context);
    if (interpreter)
        interpreter->add_timer(JS_DupValue(context, argv[0]), 0, false);
    return JS_UNDEFINED;
}

void define_function(JSContext* context, JSValueConst target, char const* name, JSCFunction* function)
{
    JS_SetPropertyStr(context, target, name, JS_NewCFunction(context, function, name, 1));
}

} // namespace

Interpreter::Interpreter()
{
    m_runtime = JS_NewRuntime();
    if (!m_runtime)
        return;
    m_context = JS_NewContext(m_runtime);
    if (!m_context)
        return;
    JS_SetContextOpaque(m_context, this);
    setup_globals();
}

Interpreter::~Interpreter()
{
    if (m_context) {
        for (auto& timer : m_timers)
            JS_FreeValue(m_context, timer.callback);
        m_timers.clear();
        JS_FreeValue(m_context, m_window);
        JS_FreeValue(m_context, m_document);
        JS_FreeContext(m_context);
    }
    if (m_runtime)
        JS_FreeRuntime(m_runtime);
}

void Interpreter::setup_globals()
{
    JSValue global = JS_GetGlobalObject(m_context);

    // console
    JSValue console = JS_NewObject(m_context);
    define_function(m_context, console, "log", js_console_log);
    JS_SetPropertyStr(m_context, global, "console", console);

    // document
    JSValue document = JS_NewObject(m_context);
    define_function(m_context, document, "write", js_document_write);
    define_function(m_context, document, "writeln", js_document_write);
    define_function(m_context, document, "getElementById", js_get_element_by_id);
    define_function(m_context, document, "createElement", js_create_element);
    define_function(m_context, document, "querySelector", js_query_selector);
    define_function(m_context, document, "querySelectorAll", js_query_selector_all);
    define_function(m_context, document, "addEventListener", js_add_event_listener);
    JS_SetPropertyStr(m_context, global, "document", JS_DupValue(m_context, document));

    // window
    JSValue window = JS_NewObject(m_context);
    define_function(m_context, window, "addEventListener", js_add_event_listener);
    define_function(m_context, window, "setTimeout", js_set_timeout);
    define_function(m_context, window, "setInterval", js_set_interval);
    define_function(m_context, window, "clearTimeout", js_clear_timer);
    define_function(m_context, window, "clearInterval", js_clear_timer);
    define_function(m_context, window, "queueMicrotask", js_queue_microtask);
    JS_SetPropertyStr(m_context, global, "window", JS_DupValue(m_context, window));

    // Bare globals too: scripts call setTimeout(...) without a window prefix.
    JS_SetPropertyStr(m_context, global, "setTimeout", JS_NewCFunction(m_context, js_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(m_context, global, "setInterval", JS_NewCFunction(m_context, js_set_interval, "setInterval", 2));
    JS_SetPropertyStr(m_context, global, "clearTimeout", JS_NewCFunction(m_context, js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(m_context, global, "clearInterval", JS_NewCFunction(m_context, js_clear_timer, "clearInterval", 1));
    JS_SetPropertyStr(m_context, global, "queueMicrotask", JS_NewCFunction(m_context, js_queue_microtask, "queueMicrotask", 1));

    JS_FreeValue(m_context, global);
    m_document = document;
    m_window = window;
}

void Interpreter::record_exception()
{
    JSValue exception = JS_GetException(m_context);
    std::string message;
    if (JS_IsObject(exception)) {
        JSValue stack = JS_GetPropertyStr(m_context, exception, "message");
        if (!JS_IsUndefined(stack))
            message = to_string(m_context, stack);
        JS_FreeValue(m_context, stack);
    }
    if (message.empty())
        message = to_string(m_context, exception);
    JS_FreeValue(m_context, exception);

    m_has_error = true;
    if (m_error.empty())
        m_error = message.empty() ? "javascript error" : message;
}

bool Interpreter::run(std::string const& source)
{
    m_has_error = false;
    m_error.clear();
    if (!m_context)
        return false;

    JSValue result = JS_Eval(m_context, source.c_str(), source.size(), "<script>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        record_exception();
        JS_FreeValue(m_context, result);
        drain_microtasks();
        return false;
    }
    JS_FreeValue(m_context, result);
    drain_microtasks();
    return !m_has_error;
}

void Interpreter::drain_microtasks()
{
    if (!m_runtime)
        return;
    for (int guard = 0; guard < 100000; ++guard) {
        JSContext* job_context = nullptr;
        int state = JS_ExecutePendingJob(m_runtime, &job_context);
        if (state == 0)
            break;
        if (state < 0) {
            if (job_context)
                JS_GetException(job_context);
            record_exception();
            break;
        }
    }
}

void Interpreter::dispatch_event_to(JSValue target, std::string const& type, std::function<JSValue(JSValue)> const& next_in_chain)
{
    if (!m_context)
        return;

    std::string on_property = "on" + type;
    std::string handler_key = "__handlers_" + type;

    JSValue current = JS_DupValue(m_context, target);
    for (int depth = 0; depth < 64; ++depth) {
        if (JS_IsUndefined(current) || JS_IsNull(current) || !JS_IsObject(current))
            break;

        JSValue handler = JS_GetPropertyStr(m_context, current, on_property.c_str());
        if (JS_IsFunction(m_context, handler)) {
            JSValue result = JS_Call(m_context, handler, current, 0, nullptr);
            if (JS_IsException(result))
                record_exception();
            JS_FreeValue(m_context, result);
        }
        JS_FreeValue(m_context, handler);

        JSValue list = JS_GetPropertyStr(m_context, current, handler_key.c_str());
        if (JS_IsArray(list)) {
            JSValue length_value = JS_GetPropertyStr(m_context, list, "length");
            uint32_t length = 0;
            JS_ToUint32(m_context, &length, length_value);
            JS_FreeValue(m_context, length_value);
            for (uint32_t index = 0; index < length; ++index) {
                JSValue entry = JS_GetPropertyUint32(m_context, list, index);
                if (JS_IsFunction(m_context, entry)) {
                    JSValue result = JS_Call(m_context, entry, current, 0, nullptr);
                    if (JS_IsException(result))
                        record_exception();
                    JS_FreeValue(m_context, result);
                }
                JS_FreeValue(m_context, entry);
            }
        }
        JS_FreeValue(m_context, list);

        if (!next_in_chain)
            break;
        JSValue parent = next_in_chain(JS_DupValue(m_context, current));
        JS_FreeValue(m_context, current);
        current = parent;
    }
    JS_FreeValue(m_context, current);
    JS_FreeValue(m_context, target); // takes ownership of the caller's reference
    drain_microtasks();
}


int Interpreter::add_timer(JSValue callback, double delay_ms, bool repeating)
{
    Timer timer;
    timer.id = m_next_timer_id++;
    timer.due = m_virtual_time + (delay_ms > 0 ? delay_ms : 0) / 1000.0;
    timer.interval = (delay_ms > 0 ? delay_ms : 0) / 1000.0;
    timer.repeating = repeating;
    timer.callback = callback;
    m_timers.push_back(timer);
    return timer.id;
}

void Interpreter::clear_timer(int id)
{
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        if (it->id != id)
            continue;
        JS_FreeValue(m_context, it->callback);
        m_timers.erase(it);
        return;
    }
}

void Interpreter::run_event_loop(int max_tasks)
{
    if (!m_context)
        return;
    for (int task = 0; task < max_tasks; ++task) {
        drain_microtasks();
        if (m_timers.empty() || m_has_error)
            break;

        auto next = std::min_element(m_timers.begin(), m_timers.end(),
            [](Timer const& left, Timer const& right) { return left.due < right.due; });
        if (next == m_timers.end())
            break;

        // Virtual time: jump to the timer instead of sleeping.
        m_virtual_time = std::max(m_virtual_time, next->due);
        Timer timer = *next;
        if (timer.repeating)
            next->due = m_virtual_time + (timer.interval > 0 ? timer.interval : 0.001);
        else
            m_timers.erase(next);

        JSValue result = JS_Call(m_context, timer.callback, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result))
            record_exception();
        JS_FreeValue(m_context, result);
        if (!timer.repeating)
            JS_FreeValue(m_context, timer.callback);
        drain_microtasks();
    }
    drain_microtasks();
}

void Interpreter::dispatch_event(std::string const& type)
{
    dispatch_event_to(JS_DupValue(m_context, m_document), type);
    dispatch_event_to(JS_DupValue(m_context, m_window), type);
}

} // namespace aetheris::js
