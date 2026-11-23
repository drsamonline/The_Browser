#include "easing.hpp"

#include <cmath>
#include <cstdlib>

namespace aetheris::rendering {

namespace {

// The cubic bezier with endpoints 0 and 1 and two control points, evaluated at a parameter.
double bezier_at(double first, double second, double parameter)
{
    double const a = 1.0 - 3.0 * second + 3.0 * first;
    double const b = 3.0 * second - 6.0 * first;
    double const c = 3.0 * first;
    return a * parameter * parameter * parameter + b * parameter * parameter + c * parameter;
}

double bezier_derivative(double first, double second, double parameter)
{
    double const a = 1.0 - 3.0 * second + 3.0 * first;
    double const b = 3.0 * second - 6.0 * first;
    double const c = 3.0 * first;
    return 3.0 * a * parameter * parameter + 2.0 * b * parameter + c;
}

double cubic_bezier(double x1, double y1, double x2, double y2, double input_progress)
{
    // https://drafts.csswg.org/css-easing-1/#cubic-bezier-algo: outside [0, 1] the curve is extended along the
    // tangent at the nearest endpoint.
    if (input_progress < 0.0) {
        if (x1 > 0.0)
            return y1 / x1 * input_progress;
        if (x2 > 0.0)
            return y2 / x2 * input_progress;
        return 0.0;
    }
    if (input_progress > 1.0) {
        if (x2 < 1.0)
            return (1.0 - y2) / (1.0 - x2) * (input_progress - 1.0) + 1.0;
        if (x1 < 1.0)
            return (1.0 - y1) / (1.0 - x1) * (input_progress - 1.0) + 1.0;
        return 1.0;
    }

    constexpr double epsilon = 1e-7;
    double parameter = input_progress;
    for (int iteration = 0; iteration < 8; ++iteration) {
        double const difference = bezier_at(x1, x2, parameter) - input_progress;
        if (std::fabs(difference) < epsilon)
            return bezier_at(y1, y2, parameter);
        double const derivative = bezier_derivative(x1, x2, parameter);
        if (std::fabs(derivative) < 1e-12)
            break;
        parameter -= difference / derivative;
    }

    double low = 0.0;
    double high = 1.0;
    parameter = input_progress;
    for (int iteration = 0; iteration < 64; ++iteration) {
        double const value = bezier_at(x1, x2, parameter);
        if (std::fabs(value - input_progress) < epsilon)
            return bezier_at(y1, y2, parameter);
        if (input_progress > value)
            low = parameter;
        else
            high = parameter;
        parameter = (low + high) / 2.0;
    }
    return bezier_at(y1, y2, parameter);
}

} // namespace

double Easing::at(double input_progress) const
{
    switch (kind) {
    case Kind::Linear:
        return input_progress;
    case Kind::CubicBezier:
        return cubic_bezier(x1, y1, x2, y2, input_progress);
    case Kind::Steps: {
        // https://www.w3.org/TR/css-easing-1/#step-easing-algo, for the two positions this engine reads.
        int const count = steps > 0 ? steps : 1;
        if (position == StepPosition::Start) {
            double const current = std::floor(input_progress * count) + 1.0;
            return std::min(1.0, current / count);
        }
        if (input_progress >= 1.0)
            return 1.0;
        return std::floor(input_progress * count) / count;
    }
    }
    return input_progress;
}

Easing easing_from_string(std::string_view name)
{
    Easing easing;
    auto const starts = [&](std::string_view prefix) { return name.rfind(prefix, 0) == 0; };

    if (name.empty() || name == "linear")
        return easing;
    if (name == "ease") {
        easing.kind = Easing::Kind::CubicBezier;
        easing.x1 = 0.25; easing.y1 = 0.1; easing.x2 = 0.25; easing.y2 = 1.0;
        return easing;
    }
    if (name == "ease-in") {
        easing.kind = Easing::Kind::CubicBezier;
        easing.x1 = 0.42; easing.y1 = 0.0; easing.x2 = 1.0; easing.y2 = 1.0;
        return easing;
    }
    if (name == "ease-out") {
        easing.kind = Easing::Kind::CubicBezier;
        easing.x1 = 0.0; easing.y1 = 0.0; easing.x2 = 0.58; easing.y2 = 1.0;
        return easing;
    }
    if (name == "ease-in-out") {
        easing.kind = Easing::Kind::CubicBezier;
        easing.x1 = 0.42; easing.y1 = 0.0; easing.x2 = 0.58; easing.y2 = 1.0;
        return easing;
    }
    if (starts("cubic-bezier(")) {
        double values[4] { 0, 0, 1, 1 };
        char const* cursor = name.data() + 13;
        for (double& value : values) {
            while (*cursor == ' ' || *cursor == ',')
                ++cursor;
            char* end = nullptr;
            value = std::strtod(cursor, &end);
            if (end == cursor)
                break;
            cursor = end;
        }
        easing.kind = Easing::Kind::CubicBezier;
        easing.x1 = values[0]; easing.y1 = values[1]; easing.x2 = values[2]; easing.y2 = values[3];
        return easing;
    }
    if (starts("steps(")) {
        char const* cursor = name.data() + 6;
        while (*cursor == ' ')
            ++cursor;
        char* end = nullptr;
        int const count = static_cast<int>(std::strtol(cursor, &end, 10));
        easing.kind = Easing::Kind::Steps;
        easing.steps = count > 0 ? count : 1;
        auto const rest = std::string_view(end ? end : cursor);
        easing.position = rest.find("start") != std::string_view::npos ? Easing::StepPosition::Start
                                                                      : Easing::StepPosition::End;
        return easing;
    }
    return easing;
}

} // namespace aetheris::rendering
