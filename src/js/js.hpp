#pragma once

#include "quickjs.h"

#include <functional>
#include <string>
#include <vector>

namespace aetheris::js {

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

    bool has_error() const { return m_has_error; }
    std::string const& error() const { return m_error; }

    // Fires an event at window and document (on<type> and addEventListener).
    void dispatch_event(std::string const& type);

    // Fires an event at one object and then up its ancestor chain, which the
    // host supplies via next_in_chain (returns JS_UNDEFINED at the root).
    void dispatch_event_to(JSValue target, std::string const& type,
        std::function<JSValue(JSValue)> const& next_in_chain = {});

    // Runs queued promise jobs (QuickJS needs this; there is no threading).
    void drain_microtasks();

    // Runs the timer queue using virtual time: timers fire in due order without
    // real waiting, up to max_tasks callbacks. This is what makes setTimeout
    // and setInterval work in a headless renderer.
    void run_event_loop(int max_tasks = 1000);

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

private:
    struct Timer {
        int id { 0 };
        double due { 0.0 };
        double interval { 0.0 };
        bool repeating { false };
        JSValue callback { JS_UNDEFINED };
    };

    void setup_globals();
    void record_exception();

    JSRuntime* m_runtime { nullptr };
    JSContext* m_context { nullptr };
    JSValue m_window { JS_UNDEFINED };
    JSValue m_document { JS_UNDEFINED };
    bool m_has_error { false };
    std::string m_error;
    std::vector<Timer> m_timers;
    int m_next_timer_id { 1 };
    double m_virtual_time { 0.0 };
};

} // namespace aetheris::js
