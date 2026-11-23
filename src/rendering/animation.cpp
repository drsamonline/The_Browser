#include "animation.hpp"
#include "style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace aetheris::rendering {

namespace {

double number_from(std::string const& text, double fallback)
{
    char* end = nullptr;
    double const value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

double seconds_from(std::string const& text)
{
    char* end = nullptr;
    double const value = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
        return 0.0;
    std::string unit(end);
    if (unit == "ms")
        return value / 1000.0;
    if (unit == "s")
        return value;
    return value / 1000.0; // a bare number on a time property is read as milliseconds, as elsewhere in the engine
}

bool is_time(std::string const& text)
{
    char* end = nullptr;
    double const value = std::strtod(text.c_str(), &end);
    // `strtod` happily parses "infinite" as infinity, so a non-finite value is not a time.
    return end != text.c_str() && std::isfinite(value);
}

} // namespace

std::string const* Keyframe::value_for(std::string_view property) const
{
    for (auto const& [name, value] : declarations)
        if (name == property)
            return &value;
    return nullptr;
}

bool KeyframesRule::value_at(double progress, std::string_view property, double& out) const
{
    // Only the frames that mention the property take part: a keyframe that is silent about it is, for that property,
    // not there at all. That is the spec's rule and it is also the one that makes `from { opacity: 0 } to {}` work.
    std::vector<Keyframe const*> relevant;
    for (auto const& frame : frames)
        if (frame.value_for(property))
            relevant.push_back(&frame);
    if (relevant.empty())
        return false;

    progress = std::clamp(progress, 0.0, 1.0);
    if (progress <= relevant.front()->offset) {
        out = number_from(*relevant.front()->value_for(property), 0.0);
        return true;
    }
    if (progress >= relevant.back()->offset) {
        out = number_from(*relevant.back()->value_for(property), 0.0);
        return true;
    }
    for (std::size_t index = 1; index < relevant.size(); ++index) {
        auto const* previous = relevant[index - 1];
        auto const* next = relevant[index];
        if (progress > next->offset)
            continue;
        double const span = next->offset - previous->offset;
        double const local = span > 0.0 ? (progress - previous->offset) / span : 0.0;
        double const from = number_from(*previous->value_for(property), 0.0);
        double const to = number_from(*next->value_for(property), 0.0);
        out = from + (to - from) * local;
        return true;
    }
    return false;
}

AnimationSpec animation_spec_from_style(StyleProperties const& style)
{
    AnimationSpec spec;
    auto const* value = style.get("animation");
    if (!value || value->empty() || *value == "none")
        return spec;

    // The first animation of a comma list, which is what this engine runs. Recorded in the row.
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
        if (part == "infinite") {
            spec.infinite = true;
            continue;
        }
        if (is_time(part)) {
            if (!have_duration) {
                spec.duration = seconds_from(part);
                have_duration = true;
            } else {
                spec.delay = seconds_from(part);
            }
            continue;
        }
        // A bare number after the first time is the iteration count.
        bool const numeric = [&] {
            char* end = nullptr;
            std::strtod(part.c_str(), &end);
            return end != part.c_str() && *end == '\0';
        }();
        if (numeric && have_duration) {
            spec.iterations = number_from(part, 1);
            continue;
        }
        if (part.rfind("cubic-bezier(", 0) == 0 || part.rfind("steps(", 0) == 0 || part == "ease"
            || part == "ease-in" || part == "ease-out" || part == "ease-in-out" || part == "linear") {
            spec.easing = easing_from_string(part);
            continue;
        }
        // What is left is the name: `animation: spin 1s linear` and `animation: 1s spin` both have one.
        if (spec.name.empty())
            spec.name = part;
    }
    spec.declared = !spec.name.empty() && spec.name != "none";
    return spec;
}

double Animation::progress_at(double now) const
{
    double const elapsed = now - start_time - delay;
    if (elapsed <= 0.0)
        return 0.0;
    if (duration <= 0.0)
        return 1.0;
    double const total = infinite ? elapsed : duration * std::max(1.0, iterations);
    if (elapsed >= total)
        return 1.0;
    double const within_iteration = std::fmod(elapsed, duration) / duration;
    return easing.at(within_iteration);
}

bool Animation::finished_at(double now) const
{
    if (infinite)
        return false;
    return now - start_time - delay >= duration * std::max(1.0, iterations);
}

bool Animation::value_at(double now, std::string_view property, double& out) const
{
    if (!rule)
        return false;
    return rule->value_at(progress_at(now), property, out);
}

} // namespace aetheris::rendering
