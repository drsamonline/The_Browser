#include "transition.hpp"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace aetheris::rendering {

namespace {

// `300ms`, `0.3s`, `300` - the last is invalid CSS but a page writes it, and reading it as milliseconds is what
// the rest of this engine does with a bare number on a time property.
double seconds_from(std::string const& token)
{
    char* end = nullptr;
    double const value = std::strtod(token.c_str(), &end);
    if (end == token.c_str())
        return 0.0;
    auto const unit = std::string(end);
    if (unit == "ms")
        return value / 1000.0;
    if (unit == "s")
        return value;
    return value / 1000.0;
}

bool is_time_token(std::string const& token)
{
    char* end = nullptr;
    std::strtod(token.c_str(), &end);
    return end != token.c_str();
}

} // namespace

double Transition::value_at(double now) const
{
    double const elapsed = now - start_time - delay;
    if (elapsed <= 0.0)
        return from;
    if (duration <= 0.0 || elapsed >= duration)
        return to;
    double const progress = elapsed / duration;
    return from + (to - from) * easing.at(progress);
}

bool Transition::finished_at(double now) const
{
    return now - start_time - delay >= duration;
}

TransitionSpec transition_spec_from_style(StyleProperties const& style)
{
    TransitionSpec spec;
    auto const* value = style.get("transition");
    if (!value || value->empty() || *value == "none")
        return spec;

    // The shorthand's order is loose: the property name, then the two times and the timing function in any
    // order. A comma starts the next transition, which this engine does not read - the first is kept.
    std::string first = *value;
    if (auto const comma = first.find(','); comma != std::string::npos)
        first = first.substr(0, comma);

    std::istringstream stream(first);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
        tokens.push_back(token);

    bool have_duration = false;
    for (auto const& part : tokens) {
        if (part == "none")
            continue;
        if (is_time_token(part)) {
            if (!have_duration) {
                spec.duration = seconds_from(part);
                have_duration = true;
            } else {
                spec.delay = seconds_from(part);
            }
            continue;
        }
        if (part.rfind("cubic-bezier(", 0) == 0 || part.rfind("steps(", 0) == 0 || part == "ease"
            || part == "ease-in" || part == "ease-out" || part == "ease-in-out" || part == "linear") {
            spec.easing = easing_from_string(part);
            continue;
        }
        spec.property = part;
    }
    spec.declared = true;
    return spec;
}

} // namespace aetheris::rendering
