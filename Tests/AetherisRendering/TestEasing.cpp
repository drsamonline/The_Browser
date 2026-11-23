// The timing-function and transition arithmetic, on its own: no font, no document, no pixels. Row 3d2 starts here
// because a transition is a curve plus a clock, and the curve can be checked without either.
#include "easing.hpp"
#include "transition.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

static void require_near(double value, double expected, double tolerance, char const* message)
{
    if (!(std::fabs(value - expected) <= tolerance)) {
        std::cerr << "FAILED: " << message << " (got " << value << ", expected " << expected << ")\n";
        std::exit(1);
    }
}

int main()
{
    // The named curves, at the endpoints and at the halfway point. `ease` is cubic-bezier(0.25, 0.1, 0.25, 1) and
    // its value at 0.5 is the standard 0.8024.
    {
        auto const linear = easing_from_string("linear");
        require_near(linear.at(0.0), 0.0, 1e-9, "linear starts at 0");
        require_near(linear.at(0.5), 0.5, 1e-9, "linear is the identity");
        require_near(linear.at(1.0), 1.0, 1e-9, "linear ends at 1");

        auto const ease = easing_from_string("ease");
        require_near(ease.at(0.0), 0.0, 1e-9, "ease starts at 0");
        require_near(ease.at(0.5), 0.8024, 1e-4, "ease at 0.5 is 0.8024");
        require_near(ease.at(1.0), 1.0, 1e-9, "ease ends at 1");

        auto const ease_in = easing_from_string("ease-in");
        auto const ease_out = easing_from_string("ease-out");
        require(ease_in.at(0.5) < 0.5, "ease-in is behind halfway");
        require(ease_out.at(0.5) > 0.5, "ease-out is ahead of it");
        require_near(easing_from_string("ease-in-out").at(0.5), 0.5, 1e-4, "ease-in-out is symmetric");

        // An explicit bezier equal to the identity is the identity, which is the solve agreeing with itself.
        require_near(easing_from_string("cubic-bezier(0, 0, 1, 1)").at(0.3), 0.3, 1e-6, "cubic-bezier(0,0,1,1) is linear");
        // Outside [0, 1] the curve extends along the endpoint tangent - and `ease` has a flat tangent there (its
        // second control point is (0.25, 1)), so it stays at 1 while `ease-in` keeps climbing.
        require_near(easing_from_string("ease").at(1.2), 1.0, 1e-9, "ease is flat past the end");
        require(easing_from_string("ease-in").at(1.2) > 1.0, "ease-in keeps climbing past it");
    }

    // Steps: jump-end holds then jumps, jump-start jumps then holds.
    {
        auto const end = easing_from_string("steps(2)");
        require_near(end.at(0.4), 0.0, 1e-9, "steps(2, jump-end) holds at 0 through the first interval");
        require_near(end.at(0.6), 0.5, 1e-9, "and jumps to 0.5 in the second");
        require_near(end.at(1.0), 1.0, 1e-9, "reaching 1 at the end");
        auto const start = easing_from_string("steps(2, jump-start)");
        require_near(start.at(0.4), 0.5, 1e-9, "steps(2, jump-start) is already at 0.5 in the first interval");
        require_near(start.at(0.6), 1.0, 1e-9, "and at 1 in the second");
    }

    // A transition's value over its clock: before the delay, in the middle, and after the end.
    {
        Transition transition;
        transition.property = "opacity";
        transition.from = 0.0;
        transition.to = 1.0;
        transition.duration = 1.0;
        transition.delay = 0.5;
        transition.start_time = 0.0;
        transition.easing = easing_from_string("linear");

        require_near(transition.value_at(0.0), 0.0, 1e-9, "before the delay it has not started");
        require_near(transition.value_at(0.5), 0.0, 1e-9, "at the end of the delay it is still at the start");
        require_near(transition.value_at(1.0), 0.5, 1e-9, "halfway through the duration it is halfway");
        require_near(transition.value_at(1.5), 1.0, 1e-9, "at the end it has arrived");
        require_near(transition.value_at(9.0), 1.0, 1e-9, "and it stays there");
        require(!transition.finished_at(1.0), "not finished halfway");
        require(transition.finished_at(1.5), "finished at the end");

        // A zero duration is not a division by zero: it arrives immediately.
        Transition instant = transition;
        instant.duration = 0.0;
        require_near(instant.value_at(0.6), 1.0, 1e-9, "a zero duration arrives at once");
    }

    // The `transition` shorthand: property, duration, timing function, delay, in any order.
    {
        StyleProperties style;
        style.set("transition", "opacity 300ms ease-in 50ms");
        auto const spec = transition_spec_from_style(style);
        require(spec.declared, "the declaration is seen");
        require(spec.property == "opacity", "the property name is read");
        require_near(spec.duration, 0.3, 1e-9, "300ms is 0.3 seconds");
        require_near(spec.delay, 0.05, 1e-9, "50ms is 0.05 seconds");
        require_near(spec.easing.at(0.5), easing_from_string("ease-in").at(0.5), 1e-9, "and the curve");

        StyleProperties reordered;
        reordered.set("transition", "ease-out 1s transform");
        auto const second = transition_spec_from_style(reordered);
        require(second.property == "transform", "the property may come last");
        require_near(second.duration, 1.0, 1e-9, "1s is one second");

        StyleProperties none;
        require(!transition_spec_from_style(none).declared, "no declaration, no transition");
        StyleProperties explicit_none;
        explicit_none.set("transition", "none");
        require(!transition_spec_from_style(explicit_none).declared, "`none` declares nothing to animate");
    }

    std::cout << "easing: ease(0.5)=" << easing_from_string("ease").at(0.5)
              << ", steps(2) at 0.6=" << easing_from_string("steps(2)").at(0.6) << '\n';
    return 0;
}
