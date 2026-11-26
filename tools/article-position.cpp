// Row 0.9: the page renders its table of contents where the article body should be. This asks the
// layout where the article actually is, and what the boxes around it resolved to, instead of guessing
// from a screenshot.
#include "css_parser.hpp"
#include "document.hpp"
#include "layout.hpp"

#include "render_document.hpp"
#include "user_agent_stylesheet.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
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

// The class the probe is looking for, set from the command line before the search.
static std::string wanted_class;

// Depth-first search for a DOM node matching a predicate, and the layout node that was built for it.
static LayoutNode const* find_layout(LayoutNode const& node, std::string const& id, std::string const& tag, int& depth)
{
    ++depth;
    if (node.dom_node) {
        if (!id.empty()) {
            if (auto value = node.dom_node->attribute("id"); value && *value == id)
                return &node;
        }
        if (!wanted_class.empty()) {
            if (auto value = node.dom_node->attribute("class"); value
                && (" " + *value + " ").find(" " + wanted_class + " ") != std::string::npos)
                return &node;
        }
        if (id.empty() && !tag.empty() && node.dom_node->name == tag)
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
    bool show_top = false;
    bool show_overflow = false;
    // The top fifteen answers "what is worst"; comparing two builds needs the whole set, because a count that
    // moves by two cannot be explained by a list of fifteen.
    bool show_all_offenders = false;
    for (int i = 3; i < argc; ++i) {
        std::string const argument = argv[i];
        if (argument.rfind("id=", 0) == 0)
            wanted_id = argument.substr(3);
        else if (argument.rfind("tag=", 0) == 0)
            wanted_tag = argument.substr(4);
        else if (argument.rfind("class=", 0) == 0)
            wanted_class = argument.substr(6);
        else if (argument == "--top")
            show_top = true;
        else if (argument == "--overflow")
            show_overflow = true;
        else if (argument == "--all")
            show_all_offenders = true;
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

    if (show_overflow) {
        // Every container whose children reach past its own content box, worst first. This is the number a
        // restructure of the flex placement has to move: an overflow is a child sized after the parent placed
        // it, and one that accumulates down a chain (row 0.13's tail).
        struct Overflow {
            std::string name;
            float past;
            float width;
            float x;
            float y;
            std::string parent;
            float parent_x;
            float parent_width;
        };
        std::vector<Overflow> overflows;
        // A child that lies wholly outside the viewport is not overflowing anything - it is a hidden menu or
        // off-canvas UI placed there on purpose, which is correct CSS. Counting those made the metric
        // unusable: six attempts to reduce it changed nothing, because most of what it counted was design
        // (row 0.13's tail).
        float const viewport_width = document.document_rect().width;
        std::function<void(LayoutNode const&)> scan = [&](LayoutNode const& node) {
            float const right = node.box.content.x + node.box.content.width;
            float worst = 0;
            for (auto const& child : node.children) {
                if (child->display == LayoutDisplay::None)
                    continue;
                if (child->rect.x >= viewport_width || child->rect.x + child->rect.width <= 0)
                    continue;
                float const child_right = child->rect.x + child->rect.width;
                worst = std::max(worst, child_right - right);
            }
            if (worst > 1.0f && node.dom_node) {
                std::string name = node.dom_node->name;
                if (auto const id = node.dom_node->attribute("id"); id && !id->empty())
                    name += "#" + *id;
                else if (auto const cls = node.dom_node->attribute("class"); cls && !cls->empty())
                    name += "." + cls->substr(0, cls->find(' '));
                std::string parent_name = "?";
                if (node.parent && node.parent->dom_node) {
                    parent_name = node.parent->dom_node->name;
                    if (auto const id = node.parent->dom_node->attribute("id"); id && !id->empty())
                        parent_name += "#" + *id;
                    else if (auto const cls = node.parent->dom_node->attribute("class"); cls && !cls->empty())
                        parent_name += "." + cls->substr(0, cls->find(' '));
                }
                overflows.push_back({ name, worst, node.box.content.width, node.box.content.x,
                    node.box.content.y, parent_name,
                    node.parent ? node.parent->box.content.x : 0.0f,
                    node.parent ? node.parent->box.content.width : 0.0f });
            }
            for (auto const& child : node.children)
                scan(*child);
        };
        scan(document.layout_root());
        std::sort(overflows.begin(), overflows.end(),
            [](Overflow const& a, Overflow const& b) { return a.past > b.past; });
        std::printf("containers whose children reach past their content box: %zu\n", overflows.size());
        std::size_t const shown = show_all_offenders ? overflows.size() : std::min<std::size_t>(15, overflows.size());
        for (std::size_t i = 0; i < shown; ++i) {
            auto const& o = overflows[i];
            // The identity, not just the name: a class can appear twice, and a namesake sent one session
            // walking the wrong element.
            std::printf("  %-38s past=%7.1f  at x=%7.1f y=%7.1f w=%7.1f\n    in %-30s x=%7.1f w=%7.1f\n",
                o.name.c_str(), o.past, o.x, o.y, o.width, o.parent.c_str(), o.parent_x, o.parent_width);
        }
        return 0;
    }

    if (show_top) {
        // What the engine actually places in the first band of the page, in document order. Comparing this
        // against a reference render says what is missing rather than guessing from a picture.
        std::printf("layout nodes starting above y=160, in document order:\n");
        int shown = 0;
        std::function<void(LayoutNode const&)> walk = [&](LayoutNode const& node) {
            if (shown > 40)
                return;
            if (node.rect.height > 0 && node.rect.y < 160 && node.dom_node && !node.dom_node->name.empty()) {
                auto const value = [&](char const* property) {
                    auto const* found = node.style.get(property);
                    return found ? *found : std::string("-");
                };
                std::printf("  %-8s y=%6.1f h=%6.1f w=%6.1f x=%6.1f display=%-12s %s\n",
                    node.dom_node->name.c_str(), node.rect.y, node.rect.height, node.rect.width, node.rect.x,
                    value("display").c_str(),
                    node.dom_node->attribute("id") ? node.dom_node->attribute("id")->c_str() : "");
                ++shown;
            }
            for (auto const& child : node.children)
                walk(*child);
        };
        walk(document.layout_root());
        if (shown == 0)
            std::printf("  (nothing)\n");
        return 0;
    }

    if (!wanted_id.empty() || !wanted_tag.empty() || !wanted_class.empty()) {
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
                std::printf("  %-10s x=%7.1f w=%7.1f y=%7.1f h=%6.1f  display=%-12s line-height=%s\n",
                    child->dom_node ? child->dom_node->name.c_str() : "?",
                    child->rect.x, child->rect.width, child->rect.y, child->rect.height,
                    value("display").c_str(), value("line-height").c_str());
                for (auto const& grandchild : child->children) {
                    auto const inner = [&](char const* property) {
                        auto const* found = grandchild->style.get(property);
                        return found ? *found : std::string("-");
                    };
                    std::printf("    %-8s x=%7.1f w=%7.1f y=%7.1f h=%6.1f  display=%-12s\n",
                        grandchild->dom_node ? grandchild->dom_node->name.c_str() : "?",
                        grandchild->rect.x, grandchild->rect.width, grandchild->rect.y, grandchild->rect.height,
                        inner("display").c_str());
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

            // What the flex sizing sees, level by level: the number that decides whether an item gets a width.
            // Both intrinsic widths, because they answer two different questions and only one of them was
            // visible: the max is the flex base size, the min is the floor a shrink cannot go below. An item
            // that overflows is either bigger than its base or floored above the space it was given, and a
            // probe that prints one number cannot say which (row 0.13's tail).
            std::printf("\nintrinsic widths (max = flex base size, min = the shrink floor):\n");
            std::function<void(LayoutNode const&, int)> widths = [&](LayoutNode const& node, int level) {
                if (level > 4)
                    return;
                std::printf("  %*s%-8s max=%7.2f  min=%7.2f  rect.w=%7.2f  %s\n", level * 2, "",
                    node.dom_node && !node.dom_node->name.empty() ? node.dom_node->name.c_str() : "(anonymous)",
                    LayoutEngine::intrinsic_width(node), LayoutEngine::intrinsic_min_width(node), node.rect.width,
                    node.dom_node && node.dom_node->type == DomNodeType::Text ? "text" : "");
                for (auto const& child : node.children)
                    widths(*child, level + 1);
            };
            widths(*target, 0);

            // The whole chain, because a box's position is usually decided above it: which ancestor sets the
            // x, and with what formatting context (row 0.13's tail cost three reverted attempts to learn
            // that guessing the layer is not good enough).
            std::printf("\nancestors, with what decides the x:\n");
            int level = 0;
            for (auto const* current = target; current && level < 8; current = current->parent, ++level) {
                auto const value = [&](char const* property) {
                    auto const* found = current->style.get(property);
                    return found ? *found : std::string("-");
                };
                std::printf("  %*s%-10s x=%7.1f w=%7.1f  content.x=%7.1f content.w=%7.1f  display=%-12s justify=%-12s flex-basis=%s\n",
                    level * 2, "",
                    current->dom_node && !current->dom_node->name.empty() ? current->dom_node->name.c_str() : "(anon)",
                    current->rect.x, current->rect.width, current->box.content.x, current->box.content.width,
                    value("display").c_str(), value("justify-content").c_str(), value("flex-basis").c_str());
            }
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
