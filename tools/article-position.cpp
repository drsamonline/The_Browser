// Row 0.9: the page renders its table of contents where the article body should be. This asks the
// layout where the article actually is, and what the boxes around it resolved to, instead of guessing
// from a screenshot.
#include "css_parser.hpp"
#include "document.hpp"
#include "layout.hpp"

#include "render_document.hpp"
#include "user_agent_stylesheet.hpp"

#include <cstdio>
#include <fstream>
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

// Depth-first search for a DOM node matching a predicate, and the layout node that was built for it.
static LayoutNode const* find_layout(LayoutNode const& node, std::string const& id, std::string const& tag, int& depth)
{
    ++depth;
    if (node.dom_node) {
        if (!id.empty()) {
            if (auto value = node.dom_node->attribute("id"); value && *value == id)
                return &node;
        }
        if (id.empty() && node.dom_node->name == tag)
            return &node;
    }
    for (auto const& child : node.children)
        if (auto const* found = find_layout(*child, id, tag, depth))
            return found;
    return nullptr;
}

static void report(RenderDocument const& document, char const* label, std::string const& id, std::string const& tag)
{
    int depth = 0;
    auto const* node = find_layout(document.layout_root(), id, tag, depth);
    if (!node) {
        std::printf("%-24s NOT IN THE LAYOUT TREE\n", label);
        return;
    }
    auto const value = [&](char const* property) {
        auto const* found = node->style.get(property);
        return found ? *found : std::string("-");
    };
    std::printf("%-24s x=%7.1f y=%7.1f w=%7.1f h=%7.1f  display=%-12s float=%-6s position=%s\n", label,
        node->rect.x, node->rect.y, node->rect.width, node->rect.height,
        value("display").c_str(), value("float").c_str(), value("position").c_str());
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: article-position page.html page.css [id=<element id> | tag=<element name>]\n");
        return 1;
    }
    std::string wanted_id;
    std::string wanted_tag;
    for (int i = 3; i < argc; ++i) {
        std::string const argument = argv[i];
        if (argument.rfind("id=", 0) == 0)
            wanted_id = argument.substr(3);
        else if (argument.rfind("tag=", 0) == 0)
            wanted_tag = argument.substr(4);
    }
    auto const html = read_file(argv[1]);
    auto const css = read_file(argv[2]);
    if (html.empty()) {
        std::printf("cannot read %s\n", argv[1]);
        return 1;
    }

    auto document = RenderDocument::create(html, css, 1024.0f);
    auto const rect = document.document_rect();
    std::printf("page: %s (%zu bytes html, %zu bytes css)\n", argv[1], html.size(), css.size());
    std::printf("document: %.0f x %.0f\n\n", rect.width, rect.height);

    if (!wanted_id.empty() || !wanted_tag.empty()) {
        int found_depth = 0;
        auto const* target = find_layout(document.layout_root(), wanted_id, wanted_tag, found_depth);
        report(document, wanted_id.empty() ? wanted_tag.c_str() : wanted_id.c_str(), wanted_id, wanted_tag);
        if (target) {
            std::printf("\nits children:\n");
            for (auto const& child : target->children) {
                auto const value = [&](char const* property) {
                    auto const* found = child->style.get(property);
                    return found ? *found : std::string("-");
                };
                std::printf("  %-10s y=%7.1f h=%6.1f  display=%-12s line-height=%-18s font-size=%s\n",
                    child->dom_node ? child->dom_node->name.c_str() : "?",
                    child->rect.y, child->rect.height, value("display").c_str(),
                    value("line-height").c_str(), value("font-size").c_str());
                for (auto const& grandchild : child->children) {
                    auto const inner = [&](char const* property) {
                        auto const* found = grandchild->style.get(property);
                        return found ? *found : std::string("-");
                    };
                    std::printf("    %-8s y=%7.1f h=%6.1f  display=%-12s line-height=%-18s font-size=%s\n",
                        grandchild->dom_node ? grandchild->dom_node->name.c_str() : "?",
                        grandchild->rect.y, grandchild->rect.height, inner("display").c_str(),
                        inner("line-height").c_str(), inner("font-size").c_str());
                }
            }
        }

        if (target && target->dom_node) {
            StyleResolver resolver;
            CssStyleSheet const empty;
            auto const baseline = resolver.resolve(*target->dom_node, empty, nullptr, nullptr).fingerprint();
            std::printf("\nrules that change this node:\n");
            int shown = 0;
            for (auto const& rule : document.stylesheet().rules) {
                CssStyleSheet single;
                single.rules.push_back(rule);
                if (resolver.resolve(*target->dom_node, single, nullptr, nullptr).fingerprint() == baseline)
                    continue;
                bool any = false;
                for (auto const& declaration : rule.declarations) {
                    auto const& property = declaration.property;
                    if (property.find("margin") == std::string::npos && property.find("padding") == std::string::npos
                        && property.find("line-height") == std::string::npos && property != "display"
                        && property != "height" && property != "font-size")
                        continue;
                    if (!any)
                        std::printf("  [%s]\n", rule.selectors.empty() ? "" : rule.selectors.front().c_str());
                    any = true;
                    std::printf("      %-20s %s\n", property.c_str(), declaration.value.c_str());
                    ++shown;
                }
            }
            if (shown == 0)
                std::printf("  (no rule sets spacing on it)\n");
        }
        return 0;
    }

    report(document, "body", "mw-content-text", "");
    report(document, "article container", "content", "");
    report(document, "first h1", "", "h1");
    report(document, "first h2", "", "h2");
    report(document, "table of contents", "toc", "");
    report(document, "sidebar column", "mw-panel", "");

    // Which rules put the h1 where it is? Each rule is resolved on its own and compared against an
    // empty sheet, so the answer comes from the engine's own matcher without widening its API.
    int depth = 0;
    auto const* h1 = find_layout(document.layout_root(), "", "h1", depth);
    if (h1 && h1->dom_node) {
        StyleResolver resolver;
        CssStyleSheet const empty;
        auto const baseline = resolver.resolve(*h1->dom_node, empty, nullptr, nullptr).fingerprint();
        std::printf("\nrules that move the first h1:\n");
        int printed = 0;
        for (auto const& rule : document.stylesheet().rules) {
            CssStyleSheet single;
            single.rules.push_back(rule);
            if (resolver.resolve(*h1->dom_node, single, nullptr, nullptr).fingerprint() == baseline)
                continue;
            bool any = false;
            for (auto const& declaration : rule.declarations) {
                auto const& property = declaration.property;
                if (property.find("margin") == std::string::npos && property.find("padding") == std::string::npos
                    && property != "float" && property != "width" && property != "left" && property != "right"
                    && property != "position" && property != "display" && property != "inset")
                    continue;
                if (!any)
                    std::printf("  [%s]\n", rule.selectors.empty() ? "" : rule.selectors.front().c_str());
                any = true;
                std::printf("      %-24s %s\n", property.c_str(), declaration.value.c_str());
                ++printed;
            }
        }
        if (printed == 0)
            std::printf("  (no rule sets a box property on it)\n");

        // Two measurements of the same text: what the flex base used, and what the box ended up.
        if (!h1->text_fragments.empty() || (h1->dom_node && !h1->dom_node->children.empty())) {
            std::printf("\n  h1 rect.width = %.2f\n", h1->rect.width);
            std::printf("  h1 text fragments = %zu\n", h1->text_fragments.size());
            if (!h1->text_fragments.empty()) {
                float widest = 0;
                for (auto const& fragment : h1->text_fragments)
                    widest = std::max(widest, fragment.rect.width);
                std::printf("  widest fragment = %.2f\n", widest);
            }
        }

        // Walk up from the h1: its placement is decided by an ancestor's formatting context.
        std::printf("\nancestors of the first h1:\n");
        int level = 0;
        for (auto const* current = h1; current && level < 6; current = current->parent, ++level) {
            auto const value = [&](char const* property) {
                auto const* found = current->style.get(property);
                return found ? *found : std::string("-");
            };
            std::printf("  %-14s rect x=%7.1f w=%7.1f  content x=%7.1f w=%7.1f  display=%-12s flex-direction=%s justify-content=%s\n",
                current->dom_node ? current->dom_node->name.c_str() : "?",
                current->rect.x, current->rect.width, current->box.content.x, current->box.content.width,
                value("display").c_str(), value("flex-direction").c_str(), value("justify-content").c_str());
        }
    }
    return 0;
}
