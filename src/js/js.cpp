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

JSValue js_document_active_element(JSContext* context, JSValueConst, int, JSValueConst*)
{
    auto* engine = interpreter_for(context);
    if (!engine->active_element)
        return JS_NULL;
    JSValue element = engine->active_element();
    return JS_IsUndefined(element) ? JS_NULL : element;
}

void define_function(JSContext* context, JSValueConst target, char const* name, JSCFunction* function)
{
    JS_SetPropertyStr(context, target, name, JS_NewCFunction(context, function, name, 1));
}

// --- Viewport -------------------------------------------------------------
// scrollTop, scrollLeft, scrollHeight, scrollWidth, clientHeight, clientWidth.
// One getter and one setter serve all six; the field travels in the QuickJS
// magic value, so there is no per-property function to keep in sync with the
// struct.
enum ViewportField {
    FieldScrollX = 0,
    FieldScrollY,
    FieldScrollWidth,
    FieldScrollHeight,
    FieldClientWidth,
    FieldClientHeight,
};

double viewport_value(ViewportMetrics const& metrics, int field)
{
    switch (field) {
    case FieldScrollX:
        return metrics.scroll_x;
    case FieldScrollY:
        return metrics.scroll_y;
    case FieldScrollWidth:
        return metrics.scroll_width;
    case FieldScrollHeight:
        return metrics.scroll_height;
    case FieldClientWidth:
        return metrics.client_width;
    case FieldClientHeight:
        return metrics.client_height;
    }
    return 0;
}

ViewportMetrics current_metrics(Interpreter* engine)
{
    return engine->viewport_metrics ? engine->viewport_metrics() : ViewportMetrics {};
}

JSValue js_viewport_get(JSContext* context, JSValueConst, int, JSValueConst*, int field)
{
    return JS_NewFloat64(context, viewport_value(current_metrics(interpreter_for(context)), field));
}

JSValue js_viewport_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int field)
{
    if (argc < 1)
        return JS_UNDEFINED;
    auto* engine = interpreter_for(context);
    if (!engine->scroll_target)
        return JS_UNDEFINED;
    double value = 0;
    if (JS_ToFloat64(context, &value, argv[0]) < 0)
        return JS_UNDEFINED;
    auto metrics = current_metrics(engine);
    if (field == FieldScrollX)
        engine->scroll_target(value, metrics.scroll_y);
    else
        engine->scroll_target(metrics.scroll_x, value);
    return JS_UNDEFINED;
}

JSValue js_window_scroll_to(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* engine = interpreter_for(context);
    if (!engine->scroll_target || argc < 1)
        return JS_UNDEFINED;
    double x = 0;
    double y = 0;
    if (JS_ToFloat64(context, &x, argv[0]) < 0)
        return JS_UNDEFINED;
    if (argc >= 2)
        JS_ToFloat64(context, &y, argv[1]);
    engine->scroll_target(x, y);
    return JS_UNDEFINED;
}

JSValue js_window_scroll_by(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* engine = interpreter_for(context);
    if (!engine->scroll_target || argc < 1)
        return JS_UNDEFINED;
    auto metrics = current_metrics(engine);
    double x = 0;
    double y = 0;
    if (JS_ToFloat64(context, &x, argv[0]) < 0)
        return JS_UNDEFINED;
    if (argc >= 2)
        JS_ToFloat64(context, &y, argv[1]);
    engine->scroll_target(metrics.scroll_x + x, metrics.scroll_y + y);
    return JS_UNDEFINED;
}

} // namespace

void install_viewport_properties(JSContext* context, JSValueConst target)
{
    struct Entry {
        char const* name;
        int field;
        bool writable;
    };
    // scrollHeight and friends are read-only in the DOM, and stay read-only
    // here: a script that assigns to them is making a mistake, not scrolling.
    static constexpr Entry entries[] = {
        { "scrollTop", FieldScrollY, true },
        { "scrollLeft", FieldScrollX, true },
        { "scrollHeight", FieldScrollHeight, false },
        { "scrollWidth", FieldScrollWidth, false },
        { "clientHeight", FieldClientHeight, false },
        { "clientWidth", FieldClientWidth, false },
    };

    for (auto const& entry : entries) {
        JSAtom atom = JS_NewAtom(context, entry.name);
        JSValue setter = entry.writable
            ? JS_NewCFunctionMagic(context, js_viewport_set, entry.name, 1, JS_CFUNC_generic_magic, entry.field)
            : JS_UNDEFINED;
        JS_DefinePropertyGetSet(context, target, atom,
            JS_NewCFunctionMagic(context, js_viewport_get, entry.name, 0, JS_CFUNC_generic_magic, entry.field),
            setter, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(context, atom);
    }
}

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


// File-local: the only caller is dispatch below, in this file. Without `static`
// it is an external symbol with no previous declaration, which
// -Wmissing-declarations (and -Werror) reject.
static JSValue make_event(JSContext* context, std::string const& type, JSValueConst target)
{
    auto* state = new EventState;
    state->target = JS_DupValue(context, target);
    state->current_target = JS_DupValue(context, target);

    JSValue event = JS_NewObjectClass(context, event_class_for(JS_GetRuntime(context)));
    JS_SetOpaque(event, state);
    JS_SetPropertyStr(context, event, "type", JS_NewString(context, type.c_str()));
    // Keyboard events carry the key a handler reads. Empty for every other type.
    auto const& event_key = interpreter_for(context)->event_key();
    if (!event_key.empty())
        JS_SetPropertyStr(context, event, "key", JS_NewString(context, event_key.c_str()));
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

// The interpreter whose globals are being set up. The localStorage functions are plain C functions
// taking a JSContext, and the store belongs to the interpreter, so they reach it through here - the
// same pattern the window loop uses for the page and the DOM host.
static Interpreter* g_active_interpreter { nullptr };

static Interpreter* active_interpreter()
{
    return g_active_interpreter;
}

// Upstream's Storage interface (HTML/Storage.idl): length, key(index), getItem, setItem, removeItem,
// clear. A missing key is null, not undefined, which is what `=== null` tests in pages rely on.
static JSValue js_storage_get_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = active_interpreter();
    if (!interpreter || argc < 1)
        return JS_NULL;
    auto key = to_string(context, argv[0]);
    auto& store = interpreter->storage_for_origin();
    auto found = store.find(key);
    if (found == store.end())
        return JS_NULL;
    return JS_NewString(context, found->second.c_str());
}

static JSValue js_storage_set_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = active_interpreter();
    if (!interpreter || argc < 2)
        return JS_UNDEFINED;
    interpreter->storage_for_origin()[to_string(context, argv[0])] = to_string(context, argv[1]);
    return JS_UNDEFINED;
}

static JSValue js_storage_remove_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = active_interpreter();
    if (!interpreter || argc < 1)
        return JS_UNDEFINED;
    interpreter->storage_for_origin().erase(to_string(context, argv[0]));
    return JS_UNDEFINED;
}

static JSValue js_storage_clear(JSContext*, JSValueConst, int, JSValueConst*)
{
    if (auto* interpreter = active_interpreter())
        interpreter->storage_for_origin().clear();
    return JS_UNDEFINED;
}

static JSValue js_storage_key(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* interpreter = active_interpreter();
    if (!interpreter || argc < 1)
        return JS_NULL;
    int32_t index = 0;
    JS_ToInt32(context, &index, argv[0]);
    auto& store = interpreter->storage_for_origin();
    if (index < 0 || static_cast<std::size_t>(index) >= store.size())
        return JS_NULL;
    auto it = store.begin();
    std::advance(it, index);
    return JS_NewString(context, it->first.c_str());
}

// The embedder's navigation behaviour. Set once, before any page is built.
static std::function<std::string()> g_current_url;
static std::function<void(std::string)> g_set_current_url;
static std::function<void(std::string, bool, bool)> g_request_navigation;
static std::function<std::size_t()> g_history_length;
static std::function<bool()> g_history_back;
static std::function<bool()> g_history_forward;
static std::function<bool(int)> g_history_go;
static std::function<bool(std::string const&, std::string, bool)> g_history_push_state;
static std::function<std::string()> g_history_state;
static std::function<int()> g_history_scroll_restoration;
static std::function<void(int)> g_history_set_scroll_restoration;

void set_navigation_hooks(std::function<std::string()> current_url,
    std::function<void(std::string)> set_current_url,
    std::function<void(std::string, bool, bool)> request_navigation,
    std::function<std::size_t()> history_length,
    std::function<bool()> history_back,
    std::function<bool()> history_forward,
    std::function<bool(int)> history_go)
{
    g_current_url = std::move(current_url);
    g_set_current_url = std::move(set_current_url);
    g_request_navigation = std::move(request_navigation);
    g_history_length = std::move(history_length);
    g_history_back = std::move(history_back);
    g_history_forward = std::move(history_forward);
    g_history_go = std::move(history_go);
}

void set_history_state_hooks(
    std::function<bool(std::string const&, std::string, bool)> push_state,
    std::function<std::string()> current_state,
    std::function<int()> scroll_restoration,
    std::function<void(int)> set_scroll_restoration)
{
    g_history_push_state = std::move(push_state);
    g_history_state = std::move(current_state);
    g_history_scroll_restoration = std::move(scroll_restoration);
    g_history_set_scroll_restoration = std::move(set_scroll_restoration);
}

// The session store's six members. Same shape as the local ones, a different map.
static std::map<std::string, std::string>* session_store()
{
    auto* interpreter = active_interpreter();
    return interpreter ? &interpreter->session_storage_for_origin() : nullptr;
}

static JSValue js_session_storage_get_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* store = session_store();
    if (!store || argc < 1)
        return JS_NULL;
    auto found = store->find(to_string(context, argv[0]));
    if (found == store->end())
        return JS_NULL;
    return JS_NewString(context, found->second.c_str());
}

static JSValue js_session_storage_set_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* store = session_store();
    if (!store || argc < 2)
        return JS_UNDEFINED;
    (*store)[to_string(context, argv[0])] = to_string(context, argv[1]);
    return JS_UNDEFINED;
}

static JSValue js_session_storage_remove_item(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* store = session_store();
    if (!store || argc < 1)
        return JS_UNDEFINED;
    store->erase(to_string(context, argv[0]));
    return JS_UNDEFINED;
}

static JSValue js_session_storage_clear(JSContext*, JSValueConst, int, JSValueConst*)
{
    if (auto* store = session_store())
        store->clear();
    return JS_UNDEFINED;
}

static JSValue js_session_storage_key(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    auto* store = session_store();
    if (!store || argc < 1)
        return JS_NULL;
    int32_t index = 0;
    JS_ToInt32(context, &index, argv[0]);
    if (index < 0 || static_cast<std::size_t>(index) >= store->size())
        return JS_NULL;
    auto it = store->begin();
    std::advance(it, index);
    return JS_NewString(context, it->first.c_str());
}

static JSValue js_session_storage_length(JSContext* context, JSValueConst, int, JSValueConst*)
{
    auto* store = session_store();
    return JS_NewInt32(context, store ? static_cast<int32_t>(store->size()) : 0);
}

// `location`. Upstream's Location (HTML/Location.idl) is a full URL record: href as a stringifier
// attribute, plus origin, protocol, host, hostname, port, pathname, search, hash, assign, replace and
// reload. This is href, assign and reload - what a page or a harness needs to move between documents, and
// no more. `replace()` and the component attributes are missing; row 1g3 in docs/CHECKLIST.md.
//
// Written from memory before that IDL was read, which is the wrong order; the shape happened to match.
static JSValue js_location_href_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    if (!g_current_url)
        return JS_NewString(context, "");
    return JS_NewString(context, g_current_url().c_str());
}

static JSValue js_location_href_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc >= 1 && g_request_navigation)
        g_request_navigation(to_string(context, argv[0]), false, false);
    return JS_UNDEFINED;
}

// The URL components `location` exposes (HTML/Location.idl: origin, protocol, host, hostname, port,
// pathname, search, hash). Upstream serialises them from the parsed URL record; this splits the string
// the embedder hands back, which is the same thing for the URLs this engine loads.
struct UrlParts {
    std::string origin;
    std::string path;
    std::string search;
    std::string hash;
};

static UrlParts split_url(std::string const& url)
{
    UrlParts parts;
    auto const scheme_end = url.find("://");
    auto const authority_start = scheme_end == std::string::npos ? 0 : scheme_end + 3;
    auto const path_start = url.find_first_of("/?#", authority_start);
    parts.origin = url.substr(0, path_start == std::string::npos ? url.size() : path_start);

    std::string rest = path_start == std::string::npos ? std::string() : url.substr(path_start);
    auto const hash_at = rest.find('#');
    if (hash_at != std::string::npos) {
        parts.hash = rest.substr(hash_at);
        rest = rest.substr(0, hash_at);
    }
    auto const search_at = rest.find('?');
    if (search_at != std::string::npos) {
        parts.search = rest.substr(search_at);
        rest = rest.substr(0, search_at);
    }
    parts.path = rest.empty() ? "/" : rest;
    return parts;
}

static JSValue location_component(JSContext* context, std::string UrlParts::*member)
{
    if (!g_current_url)
        return JS_NewString(context, "");
    auto const parts = split_url(g_current_url());
    return JS_NewString(context, (parts.*member).c_str());
}

static JSValue js_location_origin_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return location_component(context, &UrlParts::origin);
}

static JSValue js_location_pathname_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return location_component(context, &UrlParts::path);
}

static JSValue js_location_search_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return location_component(context, &UrlParts::search);
}

// The setters. Upstream copies the URL, replaces one component and navigates
// (HTML/Location.cpp: set_pathname, set_search, set_hash). The hash setter returns early when the
// fragment is unchanged, which is the one case that must not reload the page.
static std::string rebuild_url(std::string const& url, std::string const& path,
    std::string const& search, std::string const& hash)
{
    auto const parts = split_url(url);
    std::string out = parts.origin;
    out += path.empty() || path.front() != '/' ? "/" + path : path;
    if (!search.empty())
        out += search.front() == '?' ? search : "?" + search;
    if (!hash.empty())
        out += hash.front() == '#' ? hash : "#" + hash;
    return out;
}

static JSValue js_location_pathname_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1 || !g_current_url || !g_request_navigation)
        return JS_UNDEFINED;
    auto const parts = split_url(g_current_url());
    g_request_navigation(rebuild_url(g_current_url(), to_string(context, argv[0]), parts.search, parts.hash), false, false);
    return JS_UNDEFINED;
}

static JSValue js_location_search_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1 || !g_current_url || !g_request_navigation)
        return JS_UNDEFINED;
    auto const parts = split_url(g_current_url());
    g_request_navigation(rebuild_url(g_current_url(), parts.path, to_string(context, argv[0]), parts.hash), false, false);
    return JS_UNDEFINED;
}

static JSValue js_location_hash_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1 || !g_current_url || !g_request_navigation)
        return JS_UNDEFINED;
    auto const parts = split_url(g_current_url());
    auto value = to_string(context, argv[0]);
    // A single leading "#" is stripped, and an unchanged fragment does not navigate.
    if (!value.empty() && value.front() == '#')
        value = value.substr(1);
    std::string const current = parts.hash.empty() ? std::string() : parts.hash.substr(1);
    if (value == current)
        return JS_UNDEFINED;
    auto const url = rebuild_url(g_current_url(), parts.path, parts.search,
        value.empty() ? std::string() : "#" + value);
    // A fragment change is a same-document navigation, so the URL moves now and `location.hash`
    // reads the new value on the next statement rather than after a load that no longer happens.
    if (g_set_current_url)
        g_set_current_url(url);
    g_request_navigation(url, false, false);
    return JS_UNDEFINED;
}

static JSValue js_location_hash_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return location_component(context, &UrlParts::hash);
}

// `location.replace(url)`: navigates and overwrites the history entry, so Back does not return here
// (upstream: HTML/Location.cpp, replace() - navigate with replacement enabled).
static JSValue js_location_replace(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc >= 1 && g_request_navigation)
        g_request_navigation(to_string(context, argv[0]), false, true);
    return JS_UNDEFINED;
}

static JSValue js_location_reload(JSContext*, JSValueConst, int, JSValueConst*)
{
    if (g_request_navigation)
        g_request_navigation({}, true, false);
    return JS_UNDEFINED;
}

// `history` (HTML/History.idl: length, go, back, forward). `go(delta)` is not implemented - it needs the
// same stack this reads, but with an argument, and nothing here has needed it yet.
static JSValue js_history_length(JSContext* context, JSValueConst, int, JSValueConst*)
{
    return JS_NewInt32(context, g_history_length ? static_cast<int32_t>(g_history_length()) : 0);
}

static JSValue js_history_back(JSContext*, JSValueConst, int, JSValueConst*)
{
    if (g_history_back)
        g_history_back();
    return JS_UNDEFINED;
}

static JSValue js_history_forward(JSContext*, JSValueConst, int, JSValueConst*)
{
    if (g_history_forward)
        g_history_forward();
    return JS_UNDEFINED;
}

// `history.go(delta)`: back() and forward() are go(-1) and go(+1) upstream, and go(0) reloads
// (HTML/History.cpp, delta_traverse).
static JSValue js_history_go(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    int delta = 0;
    if (argc >= 1)
        JS_ToInt32(context, &delta, argv[0]);
    if (g_history_go)
        g_history_go(delta);
    return JS_UNDEFINED;
}

// `history.pushState(data, unused, url)` / `replaceState`: the URL and the session history move, nothing is loaded
// (HTML/History.cpp:169-223). The state is serialised to JSON here because this engine has no structured clone; a
// value JSON cannot represent - a function, a cycle - becomes null, which is the same substitute upstream makes
// when its serialiser refuses (History.cpp:177).
static JSValue js_history_push_replace_state(JSContext* context, JSValueConst, int argc, JSValueConst* argv, bool replace)
{
    std::string serialized = "null";
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSValue json = JS_JSONStringify(context, argv[0], JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json)) {
            JS_FreeValue(context, JS_GetException(context));
        } else if (JS_IsString(json)) {
            auto const* text = JS_ToCString(context, json);
            if (text) {
                serialized = text;
                JS_FreeCString(context, text);
            }
        }
        JS_FreeValue(context, json);
    }

    std::string url;
    if (argc >= 3 && JS_IsString(argv[2])) {
        auto const* text = JS_ToCString(context, argv[2]);
        if (text) {
            url = text;
            JS_FreeCString(context, text);
        }
    }

    if (!g_history_push_state || !g_history_push_state(url, std::move(serialized), replace)) {
        // Upstream throws a SecurityError DOMException for a cross-origin URL; this engine has no DOMException, so a
        // TypeError carries the same message and a script can still see that it failed.
        return JS_ThrowTypeError(context, "Cannot pushState or replaceState to a cross-origin URL");
    }
    return JS_UNDEFINED;
}

static JSValue js_history_push_state(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    return js_history_push_replace_state(context, this_value, argc, argv, false);
}

static JSValue js_history_replace_state(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    return js_history_push_replace_state(context, this_value, argc, argv, true);
}

// `history.state`: the serialised state of the current entry, parsed back. Null when there is none, or when the text
// is not JSON this engine can parse.
static JSValue js_history_state(JSContext* context, JSValueConst, int, JSValueConst*)
{
    if (!g_history_state)
        return JS_NULL;
    auto const serialized = g_history_state();
    if (serialized.empty())
        return JS_NULL;
    JSValue parsed = JS_ParseJSON(context, serialized.c_str(), serialized.size(), "<state>");
    if (JS_IsException(parsed)) {
        JS_FreeValue(context, JS_GetException(context));
        return JS_NULL;
    }
    return parsed;
}

static JSValue js_history_scroll_restoration(JSContext* context, JSValueConst, int, JSValueConst*)
{
    int const mode = g_history_scroll_restoration ? g_history_scroll_restoration() : 0;
    return JS_NewString(context, mode == 1 ? "manual" : "auto");
}

static JSValue js_history_set_scroll_restoration(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1 || !g_history_set_scroll_restoration)
        return JS_UNDEFINED;
    auto const* text = JS_ToCString(context, argv[0]);
    if (!text)
        return JS_UNDEFINED;
    std::string value(text);
    JS_FreeCString(context, text);
    // Anything that is not "manual" is "auto", which is the enum's only other member (HTML/History.idl).
    g_history_set_scroll_restoration(value == "manual" ? 1 : 0);
    return JS_UNDEFINED;
}

void Interpreter::setup_globals()
{
    g_active_interpreter = this;
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
    // Scroll properties live on document and on document.scrollingElement,
    // because scripts reach for either. window gets scrollX/scrollY and the two
    // scrolling functions.
    {
        JSAtom atom = JS_NewAtom(m_context, "activeElement");
        JS_DefinePropertyGetSet(m_context, document, atom,
            JS_NewCFunction(m_context, js_document_active_element, "get activeElement", 0),
            JS_UNDEFINED, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(m_context, atom);
    }
    install_viewport_properties(m_context, document);
    JSValue scrolling_element = JS_NewObject(m_context);
    install_viewport_properties(m_context, scrolling_element);
    JS_SetPropertyStr(m_context, document, "scrollingElement", scrolling_element);
    JS_SetPropertyStr(m_context, global, "document", JS_DupValue(m_context, document));

    // localStorage: one object, scoped to the origin the embedder set.
    JSValue storage = JS_NewObject(m_context);
    define_function(m_context, storage, "getItem", js_storage_get_item);
    define_function(m_context, storage, "setItem", js_storage_set_item);
    define_function(m_context, storage, "removeItem", js_storage_remove_item);
    define_function(m_context, storage, "clear", js_storage_clear);
    define_function(m_context, storage, "key", js_storage_key);
    // The same six over the session store. Upstream's Storage takes its type at construction
    // (Storage::Type::Local or Session) and everything else is identical.
    JSValue session_storage = JS_NewObject(m_context);
    define_function(m_context, session_storage, "getItem", js_session_storage_get_item);
    define_function(m_context, session_storage, "setItem", js_session_storage_set_item);
    define_function(m_context, session_storage, "removeItem", js_session_storage_remove_item);
    define_function(m_context, session_storage, "clear", js_session_storage_clear);
    define_function(m_context, session_storage, "key", js_session_storage_key);
    JSAtom session_length_atom = JS_NewAtom(m_context, "length");
    JS_DefinePropertyGetSet(m_context, session_storage, session_length_atom,
        JS_NewCFunction(m_context, js_session_storage_length, "length", 0), JS_UNDEFINED, 0);
    JS_FreeAtom(m_context, session_length_atom);
    JS_SetPropertyStr(m_context, global, "sessionStorage", session_storage);
    // `length` is an attribute in the IDL, not a function: a page reads `localStorage.length`.
    JSAtom length_atom = JS_NewAtom(m_context, "length");
    JSValue getter = JS_NewCFunction(m_context, [](JSContext* context, JSValueConst, int, JSValueConst*) -> JSValue {
        auto* interpreter = active_interpreter();
        if (!interpreter)
            return JS_NewInt32(context, 0);
        return JS_NewInt32(context, static_cast<int32_t>(interpreter->storage_for_origin().size()));
    }, "length", 0);
    JS_DefinePropertyGetSet(m_context, storage, length_atom, getter, JS_UNDEFINED, 0);
    JS_FreeAtom(m_context, length_atom);
    JS_SetPropertyStr(m_context, global, "localStorage", storage);

    // location
    JSValue location = JS_NewObject(m_context);
    define_function(m_context, location, "assign", js_location_href_set);
    define_function(m_context, location, "reload", js_location_reload);
    JSAtom href_atom = JS_NewAtom(m_context, "href");
    JS_DefinePropertyGetSet(m_context, location, href_atom,
        JS_NewCFunction(m_context, js_location_href_get, "href", 0),
        JS_NewCFunction(m_context, js_location_href_set, "href", 1), 0);
    JS_FreeAtom(m_context, href_atom);
    define_function(m_context, location, "replace", js_location_replace);
    // The read-only components. Setters are not built: upstream has them, and each one re-parses the
    // whole URL and navigates, which is row 1g5's other half.
    // `origin` is read-only; the three components are settable, each navigating.
    struct LocationComponent {
        char const* name;
        JSCFunction* getter;
        JSCFunction* setter;
    };
    for (auto const& component : std::initializer_list<LocationComponent> {
             { "origin", js_location_origin_get, nullptr },
             { "pathname", js_location_pathname_get, js_location_pathname_set },
             { "search", js_location_search_get, js_location_search_set },
             { "hash", js_location_hash_get, js_location_hash_set } }) {
        JSAtom atom = JS_NewAtom(m_context, component.name);
        JS_DefinePropertyGetSet(m_context, location, atom,
            JS_NewCFunction(m_context, component.getter, component.name, 0),
            component.setter ? JS_NewCFunction(m_context, component.setter, component.name, 1) : JS_UNDEFINED, 0);
        JS_FreeAtom(m_context, atom);
    }
    JS_SetPropertyStr(m_context, global, "location", location);

    // history
    JSValue history = JS_NewObject(m_context);
    define_function(m_context, history, "back", js_history_back);
    define_function(m_context, history, "forward", js_history_forward);
    define_function(m_context, history, "go", js_history_go);
    JSAtom history_length_atom = JS_NewAtom(m_context, "length");
    JS_DefinePropertyGetSet(m_context, history, history_length_atom,
        JS_NewCFunction(m_context, js_history_length, "length", 0), JS_UNDEFINED, 0);
    JS_FreeAtom(m_context, history_length_atom);
    define_function(m_context, history, "pushState", js_history_push_state);
    define_function(m_context, history, "replaceState", js_history_replace_state);
    {
        JSAtom state_atom = JS_NewAtom(m_context, "state");
        JS_DefinePropertyGetSet(m_context, history, state_atom,
            JS_NewCFunction(m_context, js_history_state, "state", 0), JS_UNDEFINED, 0);
        JS_FreeAtom(m_context, state_atom);
        JSAtom restoration_atom = JS_NewAtom(m_context, "scrollRestoration");
        JS_DefinePropertyGetSet(m_context, history, restoration_atom,
            JS_NewCFunction(m_context, js_history_scroll_restoration, "scrollRestoration", 0),
            JS_NewCFunction(m_context, js_history_set_scroll_restoration, "scrollRestoration", 1), 0);
        JS_FreeAtom(m_context, restoration_atom);
    }
    JS_SetPropertyStr(m_context, global, "history", history);

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
    for (auto const& alias : { std::pair<char const*, int> { "scrollX", FieldScrollX },
             std::pair<char const*, int> { "scrollY", FieldScrollY } }) {
        JSAtom atom = JS_NewAtom(m_context, alias.first);
        JS_DefinePropertyGetSet(m_context, window, atom,
            JS_NewCFunctionMagic(m_context, js_viewport_get, alias.first, 0, JS_CFUNC_generic_magic, alias.second),
            JS_UNDEFINED, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(m_context, atom);
    }
    JS_SetPropertyStr(m_context, window, "scrollTo", JS_NewCFunction(m_context, js_window_scroll_to, "scrollTo", 2));
    JS_SetPropertyStr(m_context, window, "scrollBy", JS_NewCFunction(m_context, js_window_scroll_by, "scrollBy", 2));
    JS_SetPropertyStr(m_context, global, "window", JS_DupValue(m_context, window));
    // Unqualified scrollTo/scrollBy, as setTimeout is duplicated below: window is
    // a property of the global object here, not the global object itself.
    JS_SetPropertyStr(m_context, global, "scrollTo", JS_NewCFunction(m_context, js_window_scroll_to, "scrollTo", 2));
    JS_SetPropertyStr(m_context, global, "scrollBy", JS_NewCFunction(m_context, js_window_scroll_by, "scrollBy", 2));

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

void Interpreter::fire_timer(std::vector<Timer>::iterator timer)
{
    // Virtual time: jump to the timer instead of sleeping.
    m_virtual_time = std::max(m_virtual_time, timer->due);
    Timer due = *timer;
    if (due.repeating)
        timer->due = m_virtual_time + (due.interval > 0 ? due.interval : 0.001);
    else
        m_timers.erase(timer);

    JSValue result = JS_Call(m_context, due.callback, JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(result))
        record_exception();
    JS_FreeValue(m_context, result);
    if (!due.repeating)
        JS_FreeValue(m_context, due.callback);
    drain_microtasks();
}

bool Interpreter::run_timers(double budget_ms, int max_tasks)
{
    if (!m_context)
        return false;

    double const deadline = m_virtual_time + budget_ms / 1000.0;
    bool ran = false;
    for (int task = 0; task < max_tasks; ++task) {
        drain_microtasks();
        if (m_timers.empty() || m_has_error)
            break;
        auto next = std::min_element(m_timers.begin(), m_timers.end(),
            [](Timer const& left, Timer const& right) { return left.due < right.due; });
        if (next == m_timers.end() || next->due > deadline)
            break;
        fire_timer(next);
        ran = true;
    }
    // The clock advances with the frame even when nothing was due, or a timer
    // scheduled for 50 ms would never come due in a loop ticking 16 ms at a time.
    if (m_virtual_time < deadline)
        m_virtual_time = deadline;
    return ran;
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

        fire_timer(next);
    }
    drain_microtasks();
}

void Interpreter::dispatch_key(std::string const& key)
{
    m_event_key = key;
    dispatch_event("keydown");
    m_event_key.clear();
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
