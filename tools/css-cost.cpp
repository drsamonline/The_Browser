// Row 0.1 follow-up: what a real page's own stylesheet costs. `tools/layout-cost.cpp` passes no
// author CSS, and `RenderDocument` collects every <style> a page carries, so the two measure
// different work. This one extracts the page's <style> blocks and prices them.
//
// Usage, from the repo root, after a release build:
//   zigcxx -std=c++23 -O2 -I src/rendering -I src -I third_party/harfbuzz/src \
//       -I third_party/gumbo -I third_party/quickjs tools/css-cost.cpp \
//       build-rel/lib/libAetherisRendering.a build-rel/lib/libAetherisRuntime.a \
//       build-rel/lib/libAetherisGumbo.a -L build-rel/lib -lharfbuzz \
//       -Wl,-rpath,"$PWD/build-rel/lib" /usr/lib/x86_64-linux-gnu/libwebp.so.7 \
//       /usr/lib/x86_64-linux-gnu/libssl.so.3 /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
//       -o build-rel/css-cost
//   ./build-rel/css-cost page.html
#include "document.hpp"
#include "css_parser.hpp"
#include "layout.hpp"
#include "style.hpp"
#include "user_agent_stylesheet.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace aetheris::rendering;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static std::string read_file(char const* path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Every <style>...</style> body, concatenated: what RenderDocument does with a page's inline CSS.
static std::string style_text(std::string const& html)
{
    std::string out;
    std::regex const block(R"(<style[^>]*>([\s\S]*?)</style>)");
    for (auto it = std::sregex_iterator(html.begin(), html.end(), block); it != std::sregex_iterator(); ++it)
        out += (*it)[1].str() + "\n";
    return out;
}

static void measure(std::string const& label, std::string const& html, std::string const& author_css)
{
    auto document = Document::parse_html(html);

    std::string combined;
    combined.append(user_agent_stylesheet());
    combined.push_back('\n');
    combined.append(author_css);

    auto const t0 = Clock::now();
    auto sheet = CssParser {}.parse(combined);
    auto const t1 = Clock::now();

    std::unordered_map<DomNode const*, StyleCacheEntry> cache;
    std::unordered_set<DomNode const*> const clean;
    StyleCacheStats stats;
    auto const t2 = Clock::now();
    auto root = LayoutTreeBuilder {}.build(document.root(), sheet, nullptr, &cache, &clean, &stats);
    auto const t3 = Clock::now();

    std::size_t laid_out = 0;
    LayoutReuse const counting { nullptr, nullptr, &laid_out };
    LayoutEngine { &counting }.layout(*root, 1024.0f);
    auto const t4 = Clock::now();

    std::printf("%-34s rules=%5zu  css-parse=%7.1f ms  style=%8.1f ms  layout=%7.1f ms  elements=%zu\n",
        label.c_str(), sheet.rules.size(), ms(t0, t1), ms(t2, t3), ms(t3, t4), stats.resolves);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: css-cost page.html\n");
        return 1;
    }
    auto const html = read_file(argv[1]);
    auto const css = style_text(html);
    std::printf("page: %s (%zu bytes, %zu bytes of inline CSS)\n", argv[1], html.size(), css.size());
    measure("UA stylesheet only", html, "");
    measure("UA + the page's own <style>", html, css);
    return 0;
}
