#include <render_document.hpp>

#include <css_parser.hpp>
#include <document.hpp>
#include <layout.hpp>
#include <user_agent_stylesheet.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    auto rendered = RenderDocument::create(
        "<div><span>One</span><span>Two</span><p>Three</p><aside></aside></div>",
        "div { width: 200px; min-width: 50%; max-width: 220px; }"
        "span { display: inline-block; width: 40px; height: 10px; }"
        "p { position: relative; left: 10px; top: 5px; width: 50%; }"
        "aside { position: absolute; left: 20px; top: 30px; width: 25%; height: 12px; }",
        400);

    auto const& div = *rendered.layout_root().children[0];
    require(div.box.content.width == 200, "explicit width wins");
    require(div.children[0]->display == LayoutDisplay::InlineBlock, "inline-block display");
    require(div.children[1]->rect.x >= div.children[0]->rect.x, "inline siblings laid out");
    require(div.children[2]->rect.x >= div.box.content.x + 10, "relative left offset");
    require(div.children[3]->rect.x >= div.box.content.x + 20, "absolute left offset");
    require(div.children[3]->box.content.width >= 49 && div.children[3]->box.content.width <= 51, "percentage absolute width");
    // Row 3k: `layout_inline_children` laid every child out twice - once at the whole line to
    // measure its natural width, once to place it - and measured block-level children, which have
    // no natural width to find. Each level of nesting doubled the work, so a document nested d deep
    // cost 2^d passes over the same nodes: 40 elements took 22,526 layout steps.
    {
        int const depth = 12;
        std::string html = "<!doctype html><html><body>";
        for (int i = 0; i < depth; ++i)
            html += "<div><span>level</span>";
        html += "x";
        for (int i = 0; i < depth; ++i)
            html += "</div>";
        html += "</body></html>";

        auto document = Document::parse_html(html);
        auto const sheet = CssParser {}.parse(std::string(user_agent_stylesheet()));

        std::unordered_map<DomNode const*, StyleCacheEntry> cache;
        std::unordered_set<DomNode const*> const clean;
        StyleCacheStats stats;
        auto root = LayoutTreeBuilder {}.build(document.root(), sheet, nullptr, &cache, &clean, &stats);

        std::size_t laid_out = 0;
        LayoutReuse const counting { nullptr, nullptr, &laid_out };
        LayoutEngine { &counting }.layout(*root, 800.0f);

        // One step per node, give or take the root. The bound is loose on purpose: what is being
        // pinned is the shape (linear), not an exact count that a later refactor may shift by a few.
        require(laid_out <= 100, "a deeply nested document lays out each node once, not 2^depth");

        // Cheap is not the same as correct: the nesting still has to be laid out as nesting. Each
        // div holds a line of text and then the next div, so walking the block children must visit
        // every level and each one must sit below the level that contains it.
        LayoutNode const* current = root.get();
        int levels = 0;
        while (true) {
            LayoutNode const* next_block = nullptr;
            for (auto const& child : current->children) {
                if (child->display == LayoutDisplay::Block) {
                    next_block = child.get();
                    break;
                }
            }
            if (!next_block)
                break;
            current = next_block;
            ++levels;
        }
        require(levels >= depth, "every level of the nesting is still in the layout tree");
        require(current->rect.height > 0, "the innermost box was given a height");
    }

    return 0;
}
