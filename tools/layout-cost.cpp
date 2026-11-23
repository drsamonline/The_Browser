// Row 3j: where the layout cost goes. Times the three stages of building a document - parse, style
// resolution (the layout tree build), and geometry (LayoutEngine::layout) - separately, so "0.8 ms per
// element" becomes a number with a place to look.
//
// Usage, from the repo root, after a build:
//   zigcxx -std=c++23 -O1 -I src/rendering -I src -I third_party/harfbuzz/src \
//       -I third_party/gumbo -I third_party/quickjs tools/layout-cost.cpp \
//       build/lib/libAetherisRendering.a build/lib/libAetherisRuntime.a build/lib/libAetherisGumbo.a \
//       -L build/lib -lharfbuzz -Wl,-rpath,"$PWD/build/lib" \
//       /usr/lib/x86_64-linux-gnu/libwebp.so.7 /usr/lib/x86_64-linux-gnu/libssl.so.3 \
//       /usr/lib/x86_64-linux-gnu/libcrypto.so.3 -fsanitize=undefined -o build/layout-cost
//   export AETHERIS_FONT=$(python3 -c "import matplotlib,os;print(os.path.join(os.path.dirname(matplotlib.__file__),'mpl-data/fonts/ttf/DejaVuSans.ttf'))")
//   ./build/layout-cost [page.html ...]
//
// AETHERIS_FONT must be set before the first call: active_font() is a singleton, one font per process.
#include "document.hpp"
#include "css_parser.hpp"
#include "css_stylesheet.hpp"
#include "layout.hpp"
#include "render_document.hpp"
#include "user_agent_stylesheet.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace aetheris;
using namespace aetheris::rendering;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static std::string page_of(int n, bool text)
{
    std::string html = "<!doctype html><html><body>";
    html.reserve(n * 64);
    for (int i = 0; i < n; ++i) {
        html += text ? "<div class=\"b\">item " : "<div class=\"b\">";
        if (text)
            html += "</div>";
        else
            html += "</div>";
    }
    html += "</body></html>";
    return html;
}

struct Result {
    double parse_ms { 0 };
    double style_ms { 0 };
    double layout_ms { 0 };
    std::size_t resolves { 0 };
    std::size_t hits { 0 };
    std::size_t laid_out { 0 };
};

static Result measure(std::string const& html, std::string const& css, float viewport)
{
    Result r;
    auto t0 = Clock::now();
    auto document = Document::parse_html(html);
    auto t1 = Clock::now();
    r.parse_ms = ms(t0, t1);

    std::string combined;
    combined.append(user_agent_stylesheet());
    combined.push_back('\n');
    combined.append(css);
    auto sheet = CssParser {}.parse(combined);

    std::unordered_map<DomNode const*, StyleCacheEntry> cache;
    std::unordered_set<DomNode const*> const nothing_clean;
    StyleCacheStats stats;
    auto t2 = Clock::now();
    auto layout_root = LayoutTreeBuilder {}.build(document.root(), sheet, nullptr, &cache, &nothing_clean, &stats);
    auto t3 = Clock::now();
    r.style_ms = ms(t2, t3);
    r.resolves = stats.resolves;
    r.hits = stats.hits;

    std::size_t laid_out = 0;
    LayoutReuse const counting { nullptr, nullptr, &laid_out };
    auto t4 = Clock::now();
    LayoutEngine { &counting }.layout(*layout_root, viewport);
    auto t5 = Clock::now();
    r.layout_ms = ms(t4, t5);
    r.laid_out = laid_out;
    return r;
}

int main(int argc, char** argv)
{
    std::string css = ".b { width: 100px; height: 20px; }";
    std::printf("%-28s %8s %8s %10s %10s %10s %10s\n", "document", "parse", "style", "layout", "resolves", "hits", "laid out");
    for (int n : { 100, 200, 400, 800 }) {
        for (bool text : { false, true }) {
            auto html = page_of(n, text);
            auto r = measure(html, css, 800.0f);
            std::printf("%-28s %7.1f %7.1f %10.1f %10zu %10zu %10zu\n",
                (std::to_string(n) + (text ? " divs with text" : " empty divs")).c_str(),
                r.parse_ms, r.style_ms, r.layout_ms, r.resolves, r.hits, r.laid_out);
        }
    }

    // Does style cost scale with the number of rules, or is it a fixed cost per node?
    for (int rules : { 0, 50, 200, 800 }) {
        std::string many;
        for (int i = 0; i < rules; ++i)
            many += ".r" + std::to_string(i) + " { color: #" + std::to_string(100000 + i) + "; }\n";
        auto html = page_of(400, false);
        auto r = measure(html, many, 800.0f);
        std::printf("%-28s %7.1f %7.1f %10.1f %10zu %10zu %10zu\n",
            (std::to_string(rules) + " author rules").c_str(), r.parse_ms, r.style_ms, r.layout_ms, r.resolves, r.hits,
            r.laid_out);
    }

    // Real markup: a prefix of a real page, which is where the synthetic shapes stop predicting.
    for (int i = 1; i + 1 < argc; ++i) {
        std::ifstream stream(argv[i], std::ios::binary);
        if (!stream) {
            std::printf("cannot open %s\n", argv[i]);
            continue;
        }
        std::stringstream buffer;
        buffer << stream.rdbuf();
        auto r = measure(buffer.str(), "", 800.0f);
        std::printf("%-28s %7.1f %7.1f %10.1f %10zu %10zu %10zu\n", argv[i], r.parse_ms, r.style_ms, r.layout_ms,
            r.resolves, r.hits, r.laid_out);
    }
    return 0;
}
