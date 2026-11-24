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

    // Row 0.9: a block-level child of an inline formatting context starts a new line at the container's
    // left edge. It used to be placed at the inline cursor, which on a real page put a heading at
    // x = 1000 on a 1024-wide page - off the screen - and drew a block over the text line before it.
    {
        auto rendered = RenderDocument::create(
            "<div id='outer' style='width: 300px'><span>inline text</span>"
            "<div id='inner' style='height: 20px'></div></div>", "", 400);
        auto const& outer = *rendered.layout_root().children[0];
        require(outer.children.size() == 2, "the container has a text run and a block");
        auto const& text = *outer.children[0];
        auto const& block_child = *outer.children[1];
        require(block_child.rect.x == outer.box.content.x,
            "the block starts at the container's left edge, not at the inline cursor");
        require(block_child.rect.y >= text.rect.y + text.rect.height,
            "and below the line of text, not on top of it");
    }

    // Row 0.10: a flex item that names neither a basis nor a width is sized by its content. It used to start
    // at zero, so a row of text items vanished; on a real page the same defect showed the other way round, a
    // heading given the container's full width and pushed off the edge by justify-content.
    {
        auto rendered = RenderDocument::create(
            "<div style='display: flex; width: 300px; height: 30px'>"
            "<div id='a' style='height: 30px'>abc</div>"
            "<div id='b' style='height: 30px; width: 40px'></div></div>",
            "", 400);
        auto const& row = *rendered.layout_root().children[0];
        require(row.children.size() == 2, "two flex items");
        auto const& text_item = *row.children[0];
        auto const& fixed_item = *row.children[1];
        require(fixed_item.rect.width == 40, "an item with a width keeps it");
        require(text_item.rect.width > 0, "an auto-width item is not zero - it is as wide as its text");
        require(text_item.rect.width < 300, "and it does not take the whole row");
        require(text_item.rect.x == row.box.content.x, "flex-start puts the first item at the content edge");
        require(fixed_item.rect.x == text_item.rect.x + text_item.rect.width,
            "and the second follows it, rather than overlapping");
    }

    // Row 0.11: a block inside an inline formatting context is not counted twice. The inline layout closes
    // an open line by adding its height; when the last thing placed was a block, cursor_y is already that
    // block's bottom, so adding a line again doubled it. A list item measured 33.6px where chromium gives
    // 16.8, and a real page's contents list sat 79px apart against the reference's 27.
    {
        auto rendered = RenderDocument::create(
            "<ul style='margin: 0; padding: 0; list-style: none'><li>"
            "<a href='#x' style='display: block'>Function</a></li></ul>",
            "body { margin: 0; font-size: 14px; line-height: 1.2 } li { margin: 0; padding: 0 }", 400);
        auto const& list = *rendered.layout_root().children[0];
        auto const& item = *list.children[0];
        // One line, not two: the item is as tall as the block it holds, not twice that. Comparing against
        // the child rather than a hard-coded number keeps the assertion true at any font size.
        require(!item.children.empty(), "the item holds the block");
        float const line = item.children[0]->rect.height;
        require(line > 0, "the block has a height");
        require(item.rect.height < line * 1.5f,
            "a list item holding one block line is one line tall, not two");
    }

    // Row 0.12: an image that did not load paints its `alt` text, as a browser does. A page whose logo
    // fails to fetch showed an empty box where the reference render shows the alt text.
    {
        auto rendered = RenderDocument::create(
            "<img src='file:///nope/missing.png' alt='Wikipedia' width='140' height='22'>", "", 400);
        bool drew_alt = false;
        bool drew_image = false;
        for (auto const& command : rendered.render_tree().commands()) {
            if (command.type == PaintCommand::Type::DrawImage)
                drew_image = true;
            if (command.type == PaintCommand::Type::DrawText && command.text == "Wikipedia")
                drew_alt = true;
        }
        require(!drew_image, "a missing image does not paint an image");
        require(drew_alt, "and its alt text is painted instead");

        // An image with no alt paints nothing rather than an empty string.
        auto bare = RenderDocument::create("<img src='file:///nope/other.png' width='10' height='10'>", "", 400);
        bool any_text = false;
        for (auto const& command : bare.render_tree().commands())
            if (command.type == PaintCommand::Type::DrawText)
                any_text = true;
        require(!any_text, "an image with no alt adds no text command");
    }

    return 0;
}
