#include "js.hpp"

#include <algorithm>
#include <cstring>
#include <map>

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

bool listener_is_capture(JSContext* context, JSValueConst options)
{
    if (JS_IsBool(options))
        return JS_ToBool(context, options) > 0;
    if (JS_IsObject(options)) {
        JSValue capture = JS_GetPropertyStr(context, options, "capture");
        bool result = JS_ToBool(context, capture) > 0;
        JS_FreeValue(context, capture);
        return result;
    }
    return false;
}

// True when the array already holds this exact function.
bool list_contains(JSContext* context, JSValueConst list, JSValueConst function)
{
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue entry = JS_GetPropertyUint32(context, list, index);
        bool same = JS_VALUE_GET_PTR(entry) == JS_VALUE_GET_PTR(function);
        JS_FreeValue(context, entry);
        if (same)
            return true;
    }
    return false;
}

// Rebuilds the array without every entry equal to `function`.
void list_remove(JSContext* context, JSValueConst target, std::string const& key, JSValueConst function)
{
    JSValue list = JS_GetPropertyStr(context, target, key.c_str());
    if (!JS_IsArray(list)) {
        JS_FreeValue(context, list);
        return;
    }
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);

    JSValue rebuilt = JS_NewArray(context);
    uint32_t kept = 0;
    for (uint32_t index = 0; index < length; ++index) {
        JSValue entry = JS_GetPropertyUint32(context, list, index);
        if (JS_VALUE_GET_PTR(entry) != JS_VALUE_GET_PTR(function))
            JS_SetPropertyUint32(context, rebuilt, kept++, JS_DupValue(context, entry));
        JS_FreeValue(context, entry);
    }
    JS_SetPropertyStr(context, target, key.c_str(), rebuilt);
    JS_FreeValue(context, list);
}

// addEventListener(type, handler, options) on window/document/elements.
JSValue js_add_event_listener(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    if (argc < 2 || !JS_IsFunction(context, argv[1]))
        return JS_UNDEFINED;
    bool capture = argc > 2 && listener_is_capture(context, argv[2]);
    std::string key = (capture ? "__capture_" : "__handlers_") + to_string(context, argv[0]);
    JSValue existing = JS_GetPropertyStr(context, this_value, key.c_str());
    JSValue list = existing;
    if (!JS_IsArray(list)) {
        JS_FreeValue(context, existing);
        list = JS_NewArray(context);
        JS_SetPropertyStr(context, this_value, key.c_str(), JS_DupValue(context, list));
    }
    // Adding the same listener twice is a no-op, as in the DOM.
    if (list_contains(context, list, argv[1])) {
        JS_FreeValue(context, list);
        return JS_UNDEFINED;
    }
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    JS_SetPropertyUint32(context, list, length, JS_DupValue(context, argv[1]));
    JS_FreeValue(context, list);
    return JS_UNDEFINED;
}

JSValue js_remove_event_listener(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_UNDEFINED;
    bool capture = argc > 2 && listener_is_capture(context, argv[2]);
    std::string key = (capture ? "__capture_" : "__handlers_") + to_string(context, argv[0]);
    list_remove(context, this_value, key, argv[1]);
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


// ---- event objects --------------------------------------------------------

struct EventState {
    JSValue target { JS_UNDEFINED };
    JSValue current_target { JS_UNDEFINED };
    bool default_prevented { false };
    bool propagation_stopped { false };
    bool immediate_stopped { false };
};

// A JSClassID is registered per runtime, so it cannot be a single global: the
// suite creates several interpreters (each with its own runtime).
std::map<JSRuntime*, JSClassID>& event_classes()
{
    static std::map<JSRuntime*, JSClassID> classes;
    return classes;
}

JSClassID event_class_for(JSRuntime* runtime)
{
    auto it = event_classes().find(runtime);
    return it == event_classes().end() ? 0 : it->second;
}

EventState* event_state(JSContext* context, JSValueConst value)
{
    auto id = event_class_for(JS_GetRuntime(context));
    if (id == 0)
        return nullptr;
    return static_cast<EventState*>(JS_GetOpaque(value, id));
}

JSValue js_event_prevent_default(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    if (auto* state = event_state(context, this_value))
        state->default_prevented = true;
    return JS_UNDEFINED;
}

JSValue js_event_stop_propagation(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    if (auto* state = event_state(context, this_value))
        state->propagation_stopped = true;
    return JS_UNDEFINED;
}

JSValue js_event_stop_immediate(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    if (auto* state = event_state(context, this_value)) {
        state->propagation_stopped = true;
        state->immediate_stopped = true;
    }
    return JS_UNDEFINED;
}

JSValue js_event_default_prevented(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* state = event_state(context, this_value);
    return JS_NewBool(context, state && state->default_prevented);
}

void event_finalizer(JSRuntime* runtime, JSValue value)
{
    auto id = event_class_for(runtime);
    if (id == 0)
        return;
    auto* state = static_cast<EventState*>(JS_GetOpaque(value, id));
    if (!state)
        return;
    JS_FreeValueRT(runtime, state->target);
    JS_FreeValueRT(runtime, state->current_target);
    delete state;
}


// ---- real time ------------------------------------------------------------

JSValue js_performance_now(JSContext* context, JSValueConst, int, JSValueConst*)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter)
        return JS_NewFloat64(context, 0.0);
    return JS_NewFloat64(context, interpreter->now_ms());
}

// requestAnimationFrame maps onto the timer queue one frame ahead: with virtual
// time there is no display, so a frame is a fixed step.
JSValue js_request_animation_frame(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = interpreter_for(context);
    if (!interpreter || argc < 1 || !JS_IsFunction(context, argv[0]))
        return JS_UNDEFINED;
    return JS_NewInt32(context, interpreter->add_timer(JS_DupValue(context, argv[0]), 16.0, false));
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
    {
        JSClassID event_class = 0;
        JS_NewClassID(m_runtime, &event_class);
        JSClassDef definition {};
        definition.class_name = "Event";
        definition.finalizer = event_finalizer;
        JS_NewClass(m_runtime, event_class, &definition);
        event_classes()[m_runtime] = event_class;
    }
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
    if (m_runtime) {
        event_classes().erase(m_runtime);
        JS_FreeRuntime(m_runtime);
    }
}


JSValue make_event(JSContext* context, std::string const& type, JSValueConst target)
{
    auto* state = new EventState;
    state->target = JS_DupValue(context, target);
    state->current_target = JS_DupValue(context, target);

    JSValue event = JS_NewObjectClass(context, event_class_for(JS_GetRuntime(context)));
    JS_SetOpaque(event, state);
    JS_SetPropertyStr(context, event, "type", JS_NewString(context, type.c_str()));
    JS_SetPropertyStr(context, event, "target", JS_DupValue(context, target));
    JS_SetPropertyStr(context, event, "currentTarget", JS_DupValue(context, target));
    JS_SetPropertyStr(context, event, "preventDefault", JS_NewCFunction(context, js_event_prevent_default, "preventDefault", 0));
    JS_SetPropertyStr(context, event, "stopPropagation", JS_NewCFunction(context, js_event_stop_propagation, "stopPropagation", 0));
    JS_SetPropertyStr(context, event, "stopImmediatePropagation", JS_NewCFunction(context, js_event_stop_immediate, "stopImmediatePropagation", 0));

    JSAtom atom = JS_NewAtom(context, "defaultPrevented");
    JS_DefinePropertyGetSet(context, event, atom,
        JS_NewCFunction(context, js_event_default_prevented, "get defaultPrevented", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(context, atom);
    return event;
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
    define_function(m_context, document, "removeEventListener", js_remove_event_listener);
    JS_SetPropertyStr(m_context, global, "document", JS_DupValue(m_context, document));

    // window
    JSValue window = JS_NewObject(m_context);
    define_function(m_context, window, "addEventListener", js_add_event_listener);
    define_function(m_context, window, "removeEventListener", js_remove_event_listener);
    define_function(m_context, window, "setTimeout", js_set_timeout);
    define_function(m_context, window, "setInterval", js_set_interval);
    define_function(m_context, window, "clearTimeout", js_clear_timer);
    define_function(m_context, window, "clearInterval", js_clear_timer);
    define_function(m_context, window, "queueMicrotask", js_queue_microtask);
    define_function(m_context, window, "requestAnimationFrame", js_request_animation_frame);
    define_function(m_context, window, "cancelAnimationFrame", js_clear_timer);
    JS_SetPropertyStr(m_context, global, "window", JS_DupValue(m_context, window));

    // Bare globals too: scripts call setTimeout(...) without a window prefix.
    JS_SetPropertyStr(m_context, global, "setTimeout", JS_NewCFunction(m_context, js_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(m_context, global, "setInterval", JS_NewCFunction(m_context, js_set_interval, "setInterval", 2));
    JS_SetPropertyStr(m_context, global, "clearTimeout", JS_NewCFunction(m_context, js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(m_context, global, "clearInterval", JS_NewCFunction(m_context, js_clear_timer, "clearInterval", 1));
    JS_SetPropertyStr(m_context, global, "queueMicrotask", JS_NewCFunction(m_context, js_queue_microtask, "queueMicrotask", 1));
    JS_SetPropertyStr(m_context, global, "requestAnimationFrame", JS_NewCFunction(m_context, js_request_animation_frame, "requestAnimationFrame", 1));
    JS_SetPropertyStr(m_context, global, "cancelAnimationFrame", JS_NewCFunction(m_context, js_clear_timer, "cancelAnimationFrame", 1));

    JSValue performance = JS_NewObject(m_context);
    define_function(m_context, performance, "now", js_performance_now);
    JS_SetPropertyStr(m_context, global, "performance", performance);

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
    if (!m_context) {
        JS_FreeValue(m_context, target);
        return;
    }

    // Ancestor chain: target first, then each parent.
    std::vector<JSValue> chain;
    chain.push_back(JS_DupValue(m_context, target));
    if (next_in_chain) {
        for (int depth = 0; depth < 64; ++depth) {
            JSValue parent = next_in_chain(JS_DupValue(m_context, chain.back()));
            if (!JS_IsObject(parent)) {
                JS_FreeValue(m_context, parent);
                break;
            }
            chain.push_back(parent);
        }
    }

    JSValue event = make_event(m_context, type, target);
    auto* state = event_state(m_context, event);
    m_last_event_prevented = false;

    std::string capture_key = "__capture_" + type;
    std::string bubble_key = "__handlers_" + type;
    std::string on_key = "on" + type;

    auto set_current = [&](JSValueConst node) {
        JS_SetPropertyStr(m_context, event, "currentTarget", JS_DupValue(m_context, node));
    };
    auto call_one = [&](JSValueConst node, JSValueConst handler) {
        JSValue arguments[1] = { JS_DupValue(m_context, event) };
        JSValue result = JS_Call(m_context, handler, node, 1, arguments);
        JS_FreeValue(m_context, arguments[0]);
        if (JS_IsException(result))
            record_exception();
        JS_FreeValue(m_context, result);
    };
    auto call_list = [&](JSValueConst node, std::string const& key) {
        JSValue list = JS_GetPropertyStr(m_context, node, key.c_str());
        if (JS_IsArray(list)) {
            JSValue length_value = JS_GetPropertyStr(m_context, list, "length");
            uint32_t length = 0;
            JS_ToUint32(m_context, &length, length_value);
            JS_FreeValue(m_context, length_value);
            for (uint32_t index = 0; index < length && !state->immediate_stopped; ++index) {
                JSValue handler = JS_GetPropertyUint32(m_context, list, index);
                if (JS_IsFunction(m_context, handler))
                    call_one(node, handler);
                JS_FreeValue(m_context, handler);
            }
        }
        JS_FreeValue(m_context, list);
    };
    auto call_on = [&](JSValueConst node) {
        JSValue handler = JS_GetPropertyStr(m_context, node, on_key.c_str());
        if (JS_IsFunction(m_context, handler))
            call_one(node, handler);
        JS_FreeValue(m_context, handler);
    };

    // 1. Capture: root down to (but not including) the target.
    for (std::size_t index = chain.size(); index-- > 1;) {
        if (state->propagation_stopped)
            break;
        set_current(chain[index]);
        call_list(chain[index], capture_key);
    }

    // 2. Target: on<type> plus both listener lists.
    if (!state->propagation_stopped) {
        set_current(chain.front());
        call_on(chain.front());
        call_list(chain.front(), capture_key);
        call_list(chain.front(), bubble_key);
    }

    // 3. Bubble: parent up to root.
    for (std::size_t index = 1; index < chain.size(); ++index) {
        if (state->propagation_stopped)
            break;
        set_current(chain[index]);
        call_list(chain[index], bubble_key);
        call_on(chain[index]);
    }

    m_last_event_prevented = state && state->default_prevented;

    for (auto& node : chain)
        JS_FreeValue(m_context, node);
    JS_FreeValue(m_context, event);
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
    // window is document's ancestor, so this is one chain: capture runs
    // document -> window, bubbling runs window -> document, and
    // stopPropagation stops the whole dispatch.
    dispatch_event_to(JS_DupValue(m_context, m_document), type,
        [this](JSValue current) -> JSValue {
            bool is_document = JS_VALUE_GET_PTR(current) == JS_VALUE_GET_PTR(m_document);
            JS_FreeValue(m_context, current);
            if (!is_document)
                return JS_UNDEFINED; // window is the root of the chain
            return JS_DupValue(m_context, m_window);
        });
}

} // namespace aetheris::js
