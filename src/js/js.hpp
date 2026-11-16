#pragma once

#include "quickjs.h"

#include <functional>
#include <string>
#include <vector>

namespace aetheris::js {

// What a script can see of the viewport. The host supplies it, so the JS layer
// never has to know whether it is driving a window, a headless render or a test.
struct ViewportMetrics {
    double scroll_x { 0 };
    double scroll_y { 0 };
    double scroll_width { 0 };
    double scroll_height { 0 };
    double client_width { 0 };
    double client_height { 0 };
};

// The JavaScript engine for the standalone profile, backed by the vendored
// QuickJS (third_party/quickjs). This replaces the earlier hand-written
// interpreter: the public surface is the same idea — run a script, capture
// console output, and let the host supply DOM bindings — but the language is
// now real ES2025 rather than a subset.
class Interpreter {
public:
    Interpreter();
    ~Interpreter();

    Interpreter(Interpreter const&) = delete;
    Interpreter& operator=(Interpreter const&) = delete;

    // Evaluates a script. Returns false when it threw (see error()).
    bool run(std::string const& source);

    // True when the most recent dispatch had preventDefault() called on it.
    bool last_event_prevented() const { return m_last_event_prevented; }

    bool has_error() const { return m_has_error; }
    std::string const& error() const { return m_error; }

    // Fires an event at window and document (on<type> and addEventListener).
    void dispatch_event(std::string const& type);

    // Fires `keydown` with `key` set on the event, which is what a page's
    // keyboard handler reads. The value is consumed by the next dispatch only.
    void dispatch_key(std::string const& key);
    // Sets the key the next dispatched event carries, for callers that dispatch
    // through their own chain - a focused element bubbling to its ancestors.
    void set_event_key(std::string key) { m_event_key = std::move(key); }
    std::string const& event_key() const { return m_event_key; }

    // Fires an event at one object and then up its ancestor chain, which the
    // host supplies via next_in_chain (returns JS_UNDEFINED at the root).
    void dispatch_event_to(JSValue target, std::string const& type,
        std::function<JSValue(JSValue)> const& next_in_chain = {});

    // Runs queued promise jobs (QuickJS needs this; there is no threading).
    void drain_microtasks();

    // Runs the timer queue using virtual time: timers fire in due order without
    // real waiting, up to max_tasks callbacks. This is what makes setTimeout
    // and setInterval work in a headless renderer, and it deliberately ignores
    // the clock: one call can drain a thousand firings.
    void run_event_loop(int max_tasks = 1000);

    // The same queue, bounded by a slice of virtual time: advances the clock by
    // at most `budget_ms` and fires only the timers that fall due inside it.
    // Returns true when at least one timer ran, which is the window loop's cue
    // to repaint. Without this a repeating timer either never fires again or
    // fires a thousand times in one go.
    bool run_timers(double budget_ms, int max_tasks = 16);

    // True when any timer is registered. The window loop uses this to skip the
    // document fingerprint entirely on a page with no timers.
    bool has_timers() const { return !m_timers.empty(); }

    // Milliseconds of virtual time since the interpreter started.
    double now_ms() const { return m_virtual_time * 1000.0; }

    // Registers a timer; returns its id. Used by setTimeout/setInterval.
    int add_timer(JSValue callback, double delay_ms, bool repeating);
    void clear_timer(int id);

    JSRuntime* runtime() const { return m_runtime; }
    JSContext* context() const { return m_context; }
    JSValue document_object() const { return m_document; }
    JSValue window_object() const { return m_window; }

    // Host hooks. Each returns a JS value owned by the caller's context.
    std::function<void(std::string const&)> print_handler; // console.log
    std::function<void(std::string const&)> write_handler; // document.write
    std::function<JSValue(std::string const&)> element_lookup;   // getElementById
    std::function<JSValue(std::string const&)> create_element;   // createElement
    std::function<JSValue(std::string const&)> query_selector;   // querySelector
    std::function<JSValue(std::string const&)> query_selector_all; // querySelectorAll

    // Viewport, for scrollTop/scrollLeft/scrollHeight/scrollWidth and
    // window.scrollTo/scrollBy. Both are optional: with no host wired up the
    // scroll properties read zero and writes are ignored, so `--eval` and a
    // static render still behave. Read live rather than cached, because a
    // script that scrolls and then reads scrollTop must see its own scroll.
    // document.activeElement. The host answers it, because the focused element
    // lives in the interaction state, not in the JS layer. Returning an undefined
    // value makes the property null, which is what a page with no focus expects.
    std::function<JSValue()> active_element;
    std::function<ViewportMetrics()> viewport_metrics;
    std::function<void(double, double)> scroll_target;

private:
    struct Timer {
        int id { 0 };
        double due { 0.0 };
        double interval { 0.0 };
        bool repeating { false };
        JSValue callback { JS_UNDEFINED };
    };

    void setup_globals();
    // Fires one timer and reschedules or frees it. Shared by both queue runners.
    void fire_timer(std::vector<Timer>::iterator timer);
    void record_exception();

    JSRuntime* m_runtime { nullptr };
    JSContext* m_context { nullptr };
    JSValue m_window { JS_UNDEFINED };
    JSValue m_document { JS_UNDEFINED };
    bool m_has_error { false };
    bool m_last_event_prevented { false };
    std::string m_error;
    std::vector<Timer> m_timers;
    int m_next_timer_id { 1 };
    double m_virtual_time { 0.0 };
    std::string m_event_key;
};

// Installs the six viewport properties on an object, backed by the host hooks
// on Interpreter. Declared here so a host can put them on documentElement and
// body as well as on document.
void install_viewport_properties(JSContext* context, JSValueConst target);

} // namespace aetheris::js
