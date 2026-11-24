// CSS coverage: which declarations in a real page's stylesheet does this engine actually act on?
//
// The checklist counts features; this counts declarations. For every `property: value` in a stylesheet
// it resolves one element twice - once with nothing, once with that single declaration - and compares
// the resolved style. If the style changed, the engine understood the declaration; if it did not, the
// declaration was dropped silently, which is what "the page renders unstyled" is made of.
//
// Usage, from the repo root, after a build:
//   zigcxx -std=c++23 -O2 -I src/rendering -I src -I third_party/harfbuzz/src \
//       -I third_party/gumbo -I third_party/quickjs tools/css-coverage.cpp \
//       build-rel/lib/libAetherisRendering.a build-rel/lib/libAetherisRuntime.a \
//       build-rel/lib/libAetherisGumbo.a -L build-rel/lib -lharfbuzz \
//       -Wl,-rpath,"$PWD/build-rel/lib" /usr/lib/x86_64-linux-gnu/libwebp.so.7 \
//       /usr/lib/x86_64-linux-gnu/libssl.so.3 /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
//       -o build-rel/css-coverage
//   ./build-rel/css-coverage page.css
//
// A page's `<style>` blocks can be extracted first:
//   python3 -c "import re,sys;print('\n'.join(re.findall(r'(?s)<style[^>]*>(.*?)</style>', open(sys.argv[1]).read())))" page.html > page.css
#include "css_parser.hpp"
#include "document.hpp"
#include "style.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace aetheris::rendering;

namespace {

struct Declaration {
    std::string property;
    std::string value;
};

std::string read_file(std::string const& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string trim(std::string text)
{
    auto const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    auto const end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string lower(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Every `property: value` inside a block. Crude on purpose: the point is to count what a stylesheet
// declares, not to re-implement the parser whose coverage is being measured.
std::vector<Declaration> scan(std::string const& css)
{
    std::vector<Declaration> declarations;
    std::regex const block(R"(\{([^{}]*)\})");
    for (std::sregex_iterator it(css.begin(), css.end(), block), end; it != end; ++it) {
        std::string const body = (*it)[1].str();
        std::size_t at = 0;
        while (at < body.size()) {
            auto const semicolon = body.find(';', at);
            std::string const piece = body.substr(at, semicolon == std::string::npos ? std::string::npos : semicolon - at);
            at = semicolon == std::string::npos ? body.size() : semicolon + 1;
            auto const colon = piece.find(':');
            if (colon == std::string::npos)
                continue;
            std::string property = lower(trim(piece.substr(0, colon)));
            std::string value = trim(piece.substr(colon + 1));
            // `!important` is a priority, not part of the value this engine compares.
            auto const important = value.find("!important");
            if (important != std::string::npos)
                value = trim(value.substr(0, important));
            if (property.empty() || value.empty())
                continue;
            // A name with a space or a brace is not a property; it is a selector fragment the scan
            // picked up from a nested at-rule.
            if (property.find_first_of(" \t\r\n{}") != std::string::npos)
                continue;
            if (property[0] == '@')
                continue;
            declarations.push_back({ property, value });
        }
    }
    return declarations;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: css-coverage page.css\n");
        return 1;
    }
    std::string const path = argv[1];
    auto const css = read_file(path);
    if (css.empty()) {
        std::printf("cannot read %s\n", path.c_str());
        return 1;
    }

    auto const declarations = scan(css);
    std::printf("stylesheet: %s (%zu bytes)\n", path.c_str(), css.size());
    std::printf("declarations: %zu\n\n", declarations.size());

    // One element, resolved twice per declaration. `fingerprint()` is the engine's own digest of every
    // resolved property, so "the style changed" is the engine's answer rather than this probe's guess.
    auto document = Document::parse_html("<div id='t'>x</div>");
    DomNode* target = nullptr;
    std::function<void(DomNode&)> find = [&](DomNode& node) {
        if (node.name == "div")
            target = &node;
        for (auto& child : node.children)
            find(*child);
    };
    find(document.root());
    if (!target) {
        std::printf("no element to resolve against\n");
        return 1;
    }

    StyleResolver resolver;
    auto const empty_sheet = CssParser {}.parse("");
    auto const baseline = resolver.resolve(*target, empty_sheet, nullptr, nullptr).fingerprint();

    // Count by occurrence, so a property a stylesheet leans on ranks above one it mentions once.
    std::map<std::string, std::size_t> ignored_by_property;
    std::map<std::pair<std::string, std::string>, std::size_t> ignored_pairs;
    std::map<std::string, std::size_t> understood_by_property;
    std::size_t understood = 0;
    std::size_t ignored = 0;

    for (auto const& declaration : declarations) {
        auto sheet = CssParser {}.parse("div { " + declaration.property + ": " + declaration.value + "; }");
        auto const resolved = resolver.resolve(*target, sheet, nullptr, nullptr).fingerprint();
        if (resolved != baseline) {
            ++understood;
            ++understood_by_property[declaration.property];
        } else {
            ++ignored;
            ++ignored_by_property[declaration.property];
            ++ignored_pairs[{ declaration.property, declaration.value }];
        }
    }

    std::printf("understood: %zu (%.1f%%)\n", understood,
        declarations.empty() ? 0.0 : 100.0 * static_cast<double>(understood) / static_cast<double>(declarations.size()));
    std::printf("ignored:    %zu (%.1f%%)\n\n", ignored,
        declarations.empty() ? 0.0 : 100.0 * static_cast<double>(ignored) / static_cast<double>(declarations.size()));

    auto const ranked = [](std::map<std::string, std::size_t> const& counts) {
        std::vector<std::pair<std::size_t, std::string>> out;
        out.reserve(counts.size());
        for (auto const& entry : counts)
            out.emplace_back(entry.second, entry.first);
        std::sort(out.rbegin(), out.rend());
        return out;
    };

    auto const ignored_ranked = ranked(ignored_by_property);
    std::printf("most-dropped properties (occurrences):\n");
    for (std::size_t i = 0; i < std::min<std::size_t>(25, ignored_ranked.size()); ++i)
        std::printf("  %5zu  %s\n", ignored_ranked[i].first, ignored_ranked[i].second.c_str());

    std::size_t top_sum = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(25, ignored_ranked.size()); ++i)
        top_sum += ignored_ranked[i].first;
    std::printf("\nthe 25 above account for %zu of %zu ignored declarations (%.1f%%)\n", top_sum, ignored,
        ignored ? 100.0 * static_cast<double>(top_sum) / static_cast<double>(ignored) : 0.0);

    std::vector<std::pair<std::size_t, std::pair<std::string, std::string>>> pairs;
    pairs.reserve(ignored_pairs.size());
    for (auto const& entry : ignored_pairs)
        pairs.emplace_back(entry.second, entry.first);
    std::sort(pairs.rbegin(), pairs.rend());
    std::printf("\nmost-dropped declarations:\n");
    for (std::size_t i = 0; i < std::min<std::size_t>(15, pairs.size()); ++i)
        std::printf("  %4zux  %s: %s\n", pairs[i].first, pairs[i].second.first.c_str(), pairs[i].second.second.c_str());
    return 0;
}
