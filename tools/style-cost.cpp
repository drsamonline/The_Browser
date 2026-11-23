// Row 3j, second term: what the per-node StyleProperties costs. Counts how many properties a
// resolved style actually holds, splits the resolve into matching and property work, and prices
// the map that holds them against the resolve itself.
//
// Usage, from the repo root, after a build:
//   zigcxx -std=c++23 -O1 -I src/rendering -I src -I third_party/harfbuzz/src \
//       -I third_party/gumbo -I third_party/quickjs tools/style-cost.cpp \
//       build/lib/libAetherisRendering.a build/lib/libAetherisRuntime.a build/lib/libAetherisGumbo.a \
//       -L build/lib -lharfbuzz -Wl,-rpath,"$PWD/build/lib" \
//       /usr/lib/x86_64-linux-gnu/libwebp.so.7 /usr/lib/x86_64-linux-gnu/libssl.so.3 \
//       /usr/lib/x86_64-linux-gnu/libcrypto.so.3 -fsanitize=undefined -o build/style-cost
//   export AETHERIS_FONT=$(python3 -c "import matplotlib,os;print(os.path.join(os.path.dirname(matplotlib.__file__),'mpl-data/fonts/ttf/DejaVuSans.ttf'))")
//   ./build/style-cost
//
// AETHERIS_FONT must be set before the first call: active_font() is a singleton.
//
// Run it several times and compare medians. This box varies by +-20% between identical runs, which
// is larger than the effect this probe was written to measure - see the note in
// docs/SESSION-HANDOFF-2026-11-23.md section 33.

#include "document.hpp"
#include "css_parser.hpp"
#include "layout.hpp"
#include "style.hpp"
#include "user_agent_stylesheet.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace aetheris::rendering;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void collect(DomNode& node, std::vector<DomNode*>& out)
{
    if (node.type == DomNodeType::Element)
        out.push_back(&node);
    for (auto& child : node.children)
        collect(*child, out);
}

int main()
{
    std::string html = "<!doctype html><html><body>";
    for (int i = 0; i < 400; ++i)
        html += "<div class=\"b\"></div>";
    html += "</body></html>";

    auto document = Document::parse_html(html);
    auto sheet = CssParser {}.parse(std::string(user_agent_stylesheet()) + "\n.b { width: 100px; height: 20px; }");

    std::vector<DomNode*> elements;
    collect(document.root(), elements);
    std::printf("elements: %zu\n", elements.size());

    // How many properties does a resolved style hold?
    StyleResolver resolver;
    size_t total_properties = 0;
    size_t counted = 0;
    for (auto* element : elements) {
        if (element->name != "div")
            continue;
        auto style = resolver.resolve(*element, sheet, nullptr, nullptr);
        size_t count = 0;
        style.for_each_property([&](std::string const&, std::string const&) { ++count; });
        total_properties += count;
        ++counted;
    }
    std::printf("divs: %zu, properties per resolved style: %.1f\n", counted,
        counted ? static_cast<double>(total_properties) / static_cast<double>(counted) : 0.0);
    auto const per_style = counted ? total_properties / counted : 0;

    // Time the resolve itself, over the same elements.
    auto const t0 = Clock::now();
    size_t resolved = 0;
    for (auto* element : elements) {
        if (element->name != "div")
            continue;
        auto style = resolver.resolve(*element, sheet, nullptr, nullptr);
        resolved += style.contains("width") ? 1 : 0;
    }
    auto const t1 = Clock::now();
    double const resolve_ms = ms(t0, t1);
    std::printf("resolve: %.1f ms for %zu divs = %.3f ms per element\n", resolve_ms, resolved,
        resolved ? resolve_ms / static_cast<double>(resolved) : 0.0);

    // Split the resolve: the same elements against a sheet with no rules, so what is left is the
    // property work (initial values, inheritance, the map) with no selector matching at all.
    auto const empty_sheet = CssParser {}.parse("");
    auto const e0 = Clock::now();
    size_t matched = 0;
    for (auto* element : elements) {
        if (element->name != "div")
            continue;
        auto style = resolver.resolve(*element, empty_sheet, nullptr, nullptr);
        matched += style.contains("width") ? 1 : 0;
    }
    auto const e1 = Clock::now();
    std::printf("resolve, no rules at all: %.1f ms = %.3f ms per element (so matching is the rest)\n",
        ms(e0, e1), counted ? ms(e0, e1) / static_cast<double>(counted) : 0.0);
    (void)matched;

    // Price the map alone: the same number of string-keyed inserts into an unordered_map.
    auto const t2 = Clock::now();
    size_t sink = 0;
    for (size_t n = 0; n < counted; ++n) {
        StyleProperties properties;
        for (size_t p = 0; p < per_style; ++p) {
            properties.set("property-name-" + std::to_string(p), "value-" + std::to_string(p));
        }
        sink += properties.contains("property-name-0") ? 1 : 0;
    }
    auto const t3 = Clock::now();
    double const map_ms = ms(t2, t3);
    std::printf("the map alone: %.1f ms for %zu styles = %.3f ms per style (%zu of them)\n", map_ms, counted,
        counted ? map_ms / static_cast<double>(counted) : 0.0, sink);

    // And the same inserts with a reserve, which is the cheap half of the fix.
    auto const t4 = Clock::now();
    for (size_t n = 0; n < counted; ++n) {
        std::unordered_map<std::string, std::string> map;
        map.reserve(per_style + 8);
        for (size_t p = 0; p < per_style; ++p)
            map.emplace("property-name-" + std::to_string(p), "value-" + std::to_string(p));
    }
    auto const t5 = Clock::now();
    std::printf("the map with reserve: %.1f ms = %.3f ms per style\n", ms(t4, t5),
        counted ? ms(t4, t5) / static_cast<double>(counted) : 0.0);
    return 0;
}
