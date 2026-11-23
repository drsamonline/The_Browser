#pragma once

#include "easing.hpp"
#include "style.hpp"

#include <string>

namespace aetheris::rendering {

// One property changing over time. The `transition` declaration says which property, how long it takes, how the
// progress is shaped, and how long to wait; this holds those four numbers plus the two values, and answers "what
// value is it now" for a given time on the document's clock.
//
// Upstream models a transition as an animation with a start and end time and a timing function, and asks the
// timing function for the output at the current time (CSS/CSSTransition.h:33-56, `timing_function_output_at_time`).
// This is that shape without the animation machinery around it.
struct Transition {
    std::string property;
    double from { 0 };
    double to { 0 };
    double duration { 0 }; // seconds
    double delay { 0 }; // seconds
    Easing easing;
    double start_time { 0 }; // seconds, on the document's clock

    // The value at `now`, on the same clock. Before the delay it is `from`, after the end it is `to`, and in
    // between it is the eased interpolation - which is what a repaint needs and all a repaint needs.
    double value_at(double now) const;
    bool finished_at(double now) const;
};

// The `transition` shorthand as a spec with no values in it yet: `transition: opacity 300ms ease-in 50ms`.
// A declaration naming several properties (`transition: opacity 200ms, transform 400ms`) keeps the first, which
// is a recorded limit rather than an oversight.
struct TransitionSpec {
    std::string property { "all" };
    double duration { 0 };
    double delay { 0 };
    Easing easing;
    bool declared { false };
};

TransitionSpec transition_spec_from_style(StyleProperties const& style);

} // namespace aetheris::rendering
