// Row 0.5: the engine discovers 1,324 dependencies for a page that has 188 loadable resources.
// This prints all of them, with their resolved absolute URLs and where each came from, so duplicates
// and bad base resolution can be told apart instead of guessed at.
#include "document.hpp"
#include "resource_loader.hpp"
#include "url.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace aetheris::rendering;

static std::string read_file(std::string const& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: dep-cost page.html [page-url]\n");
        return 1;
    }
    std::string const path = argv[1];
    std::string const base_url = argc > 2 ? argv[2] : ("file://" + path);
    auto const html = read_file(path);

    // The stylesheet text, to tell a dependency the markup names from one the CSS names.
    std::string css;
    std::regex const style_block(R"(<style[^>]*>([\s\S]*?)</style>)");
    for (auto it = std::sregex_iterator(html.begin(), html.end(), style_block); it != std::sregex_iterator(); ++it)
        css += (*it)[1].str();

    ResourceCache cache;
    ResourceLoader loader(cache);
    ResourceData const document { ResourceType::Document, base_url, std::vector<unsigned char>(html.begin(), html.end()) };
    auto const dependencies = loader.discover_dependencies(document, Url::parse(base_url));

    std::printf("page: %s\n  %zu bytes, %zu bytes of inline CSS\n", path.c_str(), html.size(), css.size());
    std::printf("  discovered: %zu dependencies\n\n", dependencies.size());

    std::set<std::string> distinct;
    std::map<std::string, std::size_t> repeated;
    std::size_t from_markup = 0;
    std::size_t from_css = 0;
    std::size_t from_neither = 0;
    std::vector<std::string> samples;
    samples.reserve(20);

    for (auto const& dependency : dependencies) {
        auto const serialized = dependency.serialized();
        if (!distinct.insert(serialized).second)
            ++repeated[serialized];

        // Where it came from: the path appears in the markup, or in the stylesheet, or neither -
        // "neither" means the scanner built a URL the document never spells out, which is the shape of
        // a resolution bug rather than a duplicate.
        auto const path_only = dependency.path();
        bool const in_markup = !path_only.empty() && html.find(path_only) != std::string::npos;
        bool const in_css = !path_only.empty() && css.find(path_only) != std::string::npos;
        if (in_markup)
            ++from_markup;
        else if (in_css)
            ++from_css;
        else {
            ++from_neither;
            if (samples.size() < 20)
                samples.push_back(serialized);
        }
    }

    std::printf("distinct:   %zu\n", distinct.size());
    std::printf("duplicates: %zu (over %zu URLs)\n", dependencies.size() - distinct.size(), repeated.size());
    std::printf("named in markup:    %zu\n", from_markup);
    std::printf("named in CSS:       %zu\n", from_css);
    std::printf("in neither:         %zu\n", from_neither);

    if (!repeated.empty()) {
        std::printf("\nthe most repeated:\n");
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (auto const& entry : repeated)
            ranked.emplace_back(entry.second + 1, entry.first);
        std::sort(ranked.rbegin(), ranked.rend());
        for (std::size_t i = 0; i < std::min<std::size_t>(10, ranked.size()); ++i)
            std::printf("  %4zux  %s\n", ranked[i].first, ranked[i].second.c_str());
    }

    if (!samples.empty()) {
        std::printf("\nin neither (first %zu):\n", samples.size());
        for (auto const& sample : samples)
            std::printf("  %s\n", sample.c_str());
    }
    return 0;
}
