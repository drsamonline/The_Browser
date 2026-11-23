#pragma once

#include "easing.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

// One keyframe of an `@keyframes` rule: the offset it applies at (0..1, from `from`/`0%` to `to`/`100%`) and the
// declarations it sets. Upstream keeps the same pair as `CSSKeyframeRule`'s key text plus its declaration block
// (LibWeb/CSS/CSSKeyframeRule.h, CSSKeyframesRule.h: a name and a list of rules).
struct Keyframe {
    double offset { 0 };
    std::vector<std::pair<std::string, std::string>> declarations;

    // The value this keyframe gives `property`, or null when it says nothing about it.
    std::string const* value_for(std::string_view property) const;
};

struct KeyframesRule {
    std::string name;
    std::vector<Keyframe> frames; // sorted by offset

    // The value of `property` at `progress` (0..1) through the rule, interpolated between the surrounding keyframes
    // that mention it. Numeric properties only: this engine animates numbers, and says so rather than half-parsing
    // a colour.
    bool value_at(double progress, std::string_view property, double& out) const;
};

// `animation: name duration timing-function delay iteration-count direction fill-mode` - the parts this engine reads.
// The rest of the shorthand is recorded as unread in docs/CHECKLIST.md rather than silently dropped.
struct AnimationSpec {
    std::string name;
    double duration { 0 };
    double delay { 0 };
    double iterations { 1 }; // 0 means none; a negative or "infinite" is treated as infinite
    bool infinite { false };
    Easing easing;
    bool declared { false };
};

AnimationSpec animation_spec_from_style(class StyleProperties const& style);

// A running animation: which rule, when it started, and how long it lasts.
struct Animation {
    KeyframesRule const* rule { nullptr };
    double duration { 0 };
    double delay { 0 };
    double iterations { 1 };
    bool infinite { false };
    Easing easing;
    double start_time { 0 };

    // How far through the rule the animation is at `now`, in 0..1. Outside the delay it is 0, and after the last
    // iteration it is 1.
    double progress_at(double now) const;
    bool finished_at(double now) const;

    // The animated value of a numeric property at `now`, if this animation's rule mentions it at all.
    bool value_at(double now, std::string_view property, double& out) const;
};

} // namespace aetheris::rendering
