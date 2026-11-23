#pragma once

#include <string_view>

namespace aetheris::rendering {

// A CSS timing function: what fraction of the way through a change we are at, given how far through its time we
// are. `ease`, `linear`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier(a, b, c, d)` and `steps(n)`.
//
// The cubic-bezier evaluation is upstream's, ported: Newton-Raphson for eight steps and then bisection for
// sixty-four, with the tangent extension outside [0, 1] that css-easing-1 asks for
// (Libraries/LibWeb/Rust/src/css/animation.rs:217-300, called from CSS/EasingFunction.cpp:43).
struct Easing {
    enum class Kind {
        Linear,
        CubicBezier,
        Steps,
    };
    enum class StepPosition {
        End, // `steps(n)` and `steps(n, jump-end)`: hold, then jump at the end of each interval
        Start, // `steps(n, jump-start)`: jump at the start of each interval
    };

    Kind kind { Kind::Linear };
    // Cubic bezier control points, in effect when kind is CubicBezier. `linear` is (0,0,1,1).
    double x1 { 0 };
    double y1 { 0 };
    double x2 { 1 };
    double y2 { 1 };
    int steps { 1 };
    StepPosition position { StepPosition::End };

    // The output progress for an input progress. Values outside [0, 1] extrapolate along the endpoint tangents,
    // which is what an overshooting transition needs.
    double at(double input_progress) const;
};

// Parses a timing-function name or function. An unknown name is `linear`, which is what the spec says a
// declaration with an unparseable timing function does not do - it is dropped - but this engine has nowhere to
// drop it to yet, so it is recorded as linear and the transition still runs.
Easing easing_from_string(std::string_view name);

} // namespace aetheris::rendering
