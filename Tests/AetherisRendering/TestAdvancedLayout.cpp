#include <algorithm>
#include <render_document.hpp>

#include <css_parser.hpp>
#include <document.hpp>
#include <layout.hpp>
#include <user_agent_stylesheet.hpp>

#include <cstdlib>
#include <utility>
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

    // Row 0.13: a flex item's shrink is floored at its automatic minimum size, so a line narrower than its
    // items does not collapse them to zero. Without the floor a menu of three links measured 0px wide and
    // painted nothing.
    {
        auto rendered = RenderDocument::create(
            "<div style='display: flex; width: 60px; height: 20px'>"
            "<div id='a' style='height: 20px'>Donate</div>"
            "<div id='b' style='height: 20px'>Log in</div></div>",
            "", 400);
        auto const& row = *rendered.layout_root().children[0];
        require(row.children.size() == 2, "two flex items");
        for (auto const& item : row.children) {
            require(item->rect.width > 0,
                "an item narrower than the line keeps its minimum size instead of collapsing to zero");
        }
    }

    // Row 0.13's tail: a replaced element contributes its own width to the intrinsic sizing. An `<img>` has no
    // children and no `width` style, so the max-content width of a link wrapping a 140px logo came back 0 and
    // the link was sized as if it held nothing - the largest overflow on a real page started there. Upstream
    // takes a replaced element's contribution from its natural size (replaced_sizing.rs:85-100).
    {
        auto rendered = RenderDocument::create(
            "<div style='display: flex; width: 600px'>"
            "<a id='logo' href='#x' style='display: block'><img width='140' height='22' alt='Logo'></a>"
            "</div>", "", 700);
        auto const& row = *rendered.layout_root().children[0];
        require(!row.children.empty(), "the row has the link");
        auto const& link = *row.children[0];
        require(link.rect.width >= 140.0f,
            "a link wrapping a 140px image is at least 140 wide, not zero");
    }

    // Row 0.13's tail: a flex row's intrinsic width is the sum of its items, not the widest of them. Asking for
    // the widest sized a 509px header row into a 188px box, which is where a real page's worst overflow began.
    {
        auto rendered = RenderDocument::create(
            "<div style='display: flex; gap: 20px; width: 600px'>"
            "<span id='a' style='display: block; width: 100px; height: 10px'></span>"
            "<span id='b' style='display: block; width: 120px; height: 10px'></span></div>",
            "", 700);
        auto const& row = *rendered.layout_root().children[0];
        require(row.children.size() == 2, "two flex items");
        // Each item keeps its own width, and the second follows the first plus the gap: the row is as wide as
        // both together, not as wide as the wider one.
        require(row.children[0]->rect.width == 100.0f, "the first item keeps its width");
        require(row.children[1]->rect.x == row.children[0]->rect.x + 100.0f + 20.0f,
            "and the second follows it across the gap");
    }

    // Row 0.13's tail: a run of white space between two items generates no flex item. css-flexbox-1 §4: a
    // contiguous sequence of child text runs becomes an anonymous item *unless the whole run is white space,
    // in which case it is not rendered at all*. Upstream's `can_skip_is_anonymous_text_run`
    // (`Rust/src/layout/layout_pass.rs:455`) is what `flex_formatting_context.rs:1217` uses when it generates
    // items. Without it every newline in a page's markup became an item with a width of its own and a gap on
    // each side: these two 50px boxes 10px apart were placed at x=19.6 and x=99.2 where a browser puts them at
    // 0 and 60, and a real page's header carried 195.2px of width and gap that belong to nothing.
    {
        auto rendered = RenderDocument::create(
            "<div id='row' style='display: flex; width: 200px; gap: 10px'>\n"
            "<div id='a' style='width: 50px; height: 20px'></div>\n"
            "<div id='b' style='width: 50px; height: 20px'></div>\n"
            "</div>",
            "", 400);
        auto const& row = *rendered.layout_root().children[0];
        require(row.children.size() == 5, "the text runs are still in the tree");
        require(row.children[1]->rect.x == 0.0f, "the first item starts at the row's left edge");
        require(row.children[3]->rect.x == 60.0f,
            "and the second follows it across the gap, with no item made of the white space between them");
    }

    // Row 0.13's tail: a break opportunity exists at every collapsible space, not only at U+0020. Splitting on
    // ' ' alone made a run of newlines and tabs one unbreakable word, so it contributed 9.61px to the automatic
    // minimum size of everything above it - a floor a shrink cannot go below, on a box that renders nothing.
    // A first version of this test measured "ab\tcd" against "ab" and passed on the unfixed engine, because the
    // tab is not measured either way: it could not tell the bug from its absence, so it was replaced by this.
    {
        // Two items, so the line is narrower than its contents and the shrink - and its floor - actually run.
        auto rendered = RenderDocument::create(
            "<div style='display: flex; width: 30px'>"
            "<div style='display: flex'>\n<span style='width: 20px; height: 10px'></span>\n</div>"
            "<span style='width: 20px; height: 10px'></span>"
            "</div>",
            "", 400);
        auto const& inner = *rendered.layout_root().children[0]->children[0];
        require(inner.rect.width == 20.0f,
            "white space contributes nothing to the floor: the nested row is 20px, not 20 plus a newline");
    }

    // Row 3m: a run of white space is one space wide, not as wide as the characters that write it, and in a
    // container that lays out a row of items it generates no item at all - so it adds nothing to that
    // container's maximum either. Measuring the raw text made a page's own indentation part of its content: a
    // nav measured 615.42px where 423.23px of it was content, so the row was over-full, `flex-grow` on the
    // search box never applied and the account links were pushed left (css-text-3 white-space processing;
    // upstream keeps a collapsed run as a single item and drops it at the start of a line,
    // `inline_formatting_context.rs:2931`).
    {
        // A flex item with no width takes its own maximum, so this measures the maximum directly.
        auto const max_of = [](std::string const& inner) {
            auto rendered = RenderDocument::create(
                "<div style='display: flex; width: 400px'><div style='display: flex'>" + inner + "</div></div>",
                "", 500);
            return rendered.layout_root().children[0]->children[0]->rect.width;
        };
        require(max_of("<span style='width: 20px; height: 10px'></span>") == 20.0f,
            "a row of one 20px box and no white space is 20px");
        // Before this row the two newlines measured 9.61px each - their untransformed text - and the item
        // took 39.22px. The probe prints that number per node, which is how the page's nav was found to be
        // 615.42px wide when 423.23px of it was content.
        require(max_of("\n<span style='width: 20px; height: 10px'></span>\n") == 20.0f,
            "and the newlines around it add nothing, because they generate no item");
    }

    // Row 0.16: `grid-template` is a shorthand for grid-template-rows, grid-template-columns and
    // grid-template-areas (css-grid-1 §7.3; upstream lists exactly those three longhands,
    // CSS/Properties.json:3282-3297), and a track may be `minmax(a, b)`. Neither was read, so a page whose
    // whole column layout is written with the shorthand had no tracks and no named areas.
    {
        auto const grid_of = [](char const* declaration) {
            auto rendered = RenderDocument::create(
                std::string("<div style='display:grid; width: 1024px; ") + declaration + "'>"
                "<div style='grid-area:a; height:10px'></div>"
                "<div style='grid-area:b; height:10px'></div></div>",
                "", 1024);
            auto const& grid = *rendered.layout_root().children[0];
            return std::pair<float, float> { grid.children[0]->rect.width, grid.children[1]->rect.x };
        };
        auto const longhand = grid_of("grid-template-columns:100px 1fr; grid-template-areas:'a b'");
        auto const shorthand = grid_of("grid-template: 1fr / 100px 1fr; grid-template-areas:'a b'");
        require(longhand.first == 100.0f && longhand.second == 100.0f,
            "the longhand gives a 100px first column and the second item after it");
        require(shorthand.first == longhand.first && shorthand.second == longhand.second,
            "and the grid-template shorthand gives the same columns as the longhands it stands for");
        // `minmax(0,1fr)` is how a real page writes its content column.
        auto const with_minmax = grid_of("grid-template: 1fr / 100px minmax(0,1fr); grid-template-areas:'a b'");
        require(with_minmax.first == 100.0f && with_minmax.second == 100.0f,
            "a minmax() track is read as the flexible track it names");
    }

    // Row 0.15: a percentage height resolves against the containing block's height, and against an
    // indefinite one it is `auto` (css2 §10.5; css-flexbox-1 §9.8). It was resolved against the element's own
    // **width**, which is a different quantity: `height:100%` on a 140px-wide logo became 140px tall,
    // stretched a page's header from ~68px to 156px and pushed its article down 88px.
    {
        auto rendered = RenderDocument::create(
            "<div id='row' style='display: flex; align-items: center'>"
            "<a id='logo' style='display: flex; height: 100%'>"
            "<span style='display: block; width: 100px; height: 38px'></span></a></div>",
            "", 400);
        auto const& row = *rendered.layout_root().children[0];
        require(row.rect.height == 38.0f,
            "a percentage height against an auto-height parent does not stretch it");
        require(row.children[0]->rect.height == 38.0f,
            "and the item itself is as tall as its content, not as wide as it is");
    }

    // Row 0.18: the vertical padding of an inline, non-replaced box paints outside the line box but does not
    // make the line taller (css2 §8.1, §10.6.1). It was added to the line, so a real page's table of contents
    // - whose items are `<a><div class=vector-toc-text style='padding:6px 0'>` - came out 1.7x too tall.
    {
        auto rendered = RenderDocument::create(
            "<div id='d' style='font-size: 14px; line-height: 1.2'>"
            "<a style='display: block'><span style='padding: 6px 0'>one</span></a>"
            "<a style='display: block'><span style='padding: 6px 0'>two</span></a></div>",
            "", 400);
        auto const& block = *rendered.layout_root().children[0];
        auto const gap = block.children[1]->rect.y - block.children[0]->rect.y;
        require(gap > 16.5f && gap < 17.1f,
            "an inline box's vertical padding does not push the next line down");
        require(block.rect.height < 40.0f,
            "so two 16.8px lines are 33.6px, not 57.6");
    }

    // Row 0.19: a space at the edge of a text run is part of the line when two inline items sit on it, and
    // is removed at the end of one (css-text-3 §4.1.1). TextLayout only ever emits a space *between two words
    // of its own run* (text_layout.cpp:116 discards it, :137 puts it back), so a run ending in a space stopped
    // at its last word and the next inline box was placed flush against it: "a " + <a>b</a> read "ab", which
    // on the page was "often abbreviated as" + <a>browser</a> -> "asbrowser".
    {
        auto spaced = RenderDocument::create(
            "<div id='d' style='font-size: 14px; margin: 0'>a <a href='#'>b</a></div>", "", 400);
        auto flush = RenderDocument::create(
            "<div id='d' style='font-size: 14px; margin: 0'>a<a href='#'>b</a></div>", "", 400);
        auto const& spaced_block = *spaced.layout_root().children[0];
        auto const& flush_block = *flush.layout_root().children[0];
        auto const& spaced_link = *spaced_block.children[1];
        auto const& flush_link = *flush_block.children[1];
        float const gap = spaced_link.rect.x - flush_link.rect.x;
        require(gap > 2.0f && gap < 8.0f,
            "the space before a link occupies width, so 'a b' is not 'ab'");
    }

    // The mirror case: a run that *starts* with a space loses it the same way, so it must be placed one space
    // past the inline box before it. The page has 424 boundaries of the first kind and 316 of this one.
    {
        auto leading = RenderDocument::create(
            "<div id='d' style='font-size: 14px; margin: 0'><a href='#'>a</a> b</div>", "", 400);
        auto const& block = *leading.layout_root().children[0];
        auto const& link = *block.children[0];
        auto const& run = *block.children[1];
        require(!run.text_fragments.empty(), "the run after the link has a fragment");
        require(run.text_fragments.front().rect.x > link.rect.x + link.rect.width + 2.0f,
            "the space after a link occupies width too, so 'a b' is not 'ab'");
    }

    // A trailing space before a block-level sibling sits at the end of the line, where css-text-3 removes it.
    // This one guards against over-application: it passes on the unfixed engine too, so it is not the proof.
    {
        auto blocky = RenderDocument::create(
            "<div id='d' style='font-size: 14px; margin: 0'>a <div style='height: 5px'></div></div>", "", 400);
        auto const& block = *blocky.layout_root().children[0];
        require(block.children[1]->rect.x < 0.5f,
            "a space at the end of a line does not push the next block right");
    }

    // Row 0.20: css2 §9.5 - a float leaves the normal flow, is placed against the left or right edge of its
    // containing block, is sized to its own content, and the content after it flows beside it. Not reading
    // `float` at all made a right-floated figure 953.6px wide on a 976px column.
    {
        auto rendered = RenderDocument::create(
            "<div id='d' style='width: 200px; font-size: 14px'>"
            "<div id='f' style='float: right; width: 60px; height: 40px'></div>"
            "<div id='b' style='height: 10px'></div></div>", "", 400);
        auto const& block = *rendered.layout_root().children[0];
        auto const& floated = *block.children[0];
        auto const& after = *block.children[1];
        float const right_edge = floated.rect.x + floated.rect.width;
        require(right_edge > 195.0f && right_edge < 205.0f,
            "a right float sits against the container's right edge");
        require(floated.rect.width < 100.0f,
            "a float is sized to its own content, not to the column");
        require(after.rect.y < floated.rect.y + floated.rect.height - 1.0f,
            "the block after a float starts beside it, not below it");
        // css2 §9.5: a block's *border box* overlaps a float - only its **line boxes** are shortened. So the block
        // beside the float keeps its full width, and the shortening is asserted where it actually happens, by the
        // per-line fixture below. This assertion used to demand `after.rect.width < 150.0f`, which encoded the
        // narrowing approximation instead of the spec (row 0.21).
        require(after.rect.width > 190.0f,
            "a block beside a float keeps its full border box; only its line boxes are shortened");
    }

    // The mirror: a left float takes the left edge, and the content beside it starts to its right.
    {
        auto rendered = RenderDocument::create(
            "<div id='d' style='width: 200px; font-size: 14px'>"
            "<div id='f' style='float: left; width: 60px; height: 40px'></div>"
            "<div id='b' style='height: 10px'></div></div>", "", 400);
        auto const& block = *rendered.layout_root().children[0];
        auto const& floated = *block.children[0];
        auto const& after = *block.children[1];
        require(floated.rect.x < block.box.content.x + 1.0f,
            "a left float sits against the container's left edge");
        // The same model on the other side: the block's border box is full width and its *line boxes* start to
        // the right of the float. The line-box half is asserted by the per-line fixture below (row 0.21).
        require(after.rect.width > 190.0f,
            "a block beside a left float keeps its full border box too");
    }

    // Row 0.21: a float shortens the line boxes that overlap it, and only those (css2 §9.5). Upstream makes the
    // line the unit of placement - LineBuilder::begin_new_line recomputes the available width from the float bands
    // (Rust/src/layout/line_builder.rs:2297-2304 -> inline_formatting_context.rs:1958-1970). Two things are
    // asserted, and the second is the one every earlier attempt at this row got wrong: a line that overlaps the
    // float stops short of it, and a line below the float is full width again.
    {
        auto rendered = RenderDocument::create(
            "<div id='d' style='width: 300px; font-size: 14px; line-height: 20px'>"
            "<div id='f' style='float: right; width: 100px; height: 40px'></div>"
            "<p id='p' style='margin: 0'>"
            "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau"
            "</p></div>", "", 400);
        auto const& block = *rendered.layout_root().children[0];
        auto const& floated = *block.children[0];
        auto const& para = *block.children[1];
        float const float_left = floated.rect.x;
        float const float_bottom = floated.rect.y + floated.rect.height;
        require(float_left > 150.0f && float_left < 210.0f,
            "the right float sits against the container's right edge");

        require(!para.children.empty() && !para.children[0]->text_fragments.empty(),
            "the paragraph beside the float has fragments");
        float below_float_right_edge = 0.0f;
        bool saw_line_below_the_float = false;
        for (auto const& fragment : para.children[0]->text_fragments) {
            bool const overlaps_float = fragment.rect.y < float_bottom - 0.5f;
            if (overlaps_float)
                require(fragment.rect.x + fragment.rect.width <= float_left + 0.5f,
                    "a line that overlaps a float stops short of it");
            else {
                saw_line_below_the_float = true;
                below_float_right_edge = std::max(below_float_right_edge, fragment.rect.x + fragment.rect.width);
            }
        }
        require(saw_line_below_the_float, "the paragraph continues below the float");
        require(below_float_right_edge > float_left,
            "a line below the float is full width again, so the intrusion is per line and not per block");
    }

    // The case the page exposed: a word that fits the full line but not the room beside the float must drop
    // past the float rather than be drawn over it. Upstream jumps to the next band start
    // (Rust/src/layout/line_builder.rs:358-362, next_float_band_block_start_after); without that step a line
    // ended 17.4px inside the float's box on the real page.
    {
        auto rendered = RenderDocument::create(
            "<div id='d' style='width: 300px; font-size: 14px; line-height: 20px'>"
            "<div id='f' style='float: right; width: 100px; height: 40px'></div>"
            "<p id='p' style='margin: 0'>supercalifragilisticexpialidocious tail</p></div>", "", 400);
        auto const& block = *rendered.layout_root().children[0];
        auto const& floated = *block.children[0];
        auto const& para = *block.children[1];
        float const float_left = floated.rect.x;
        float const float_bottom = floated.rect.y + floated.rect.height;
        require(!para.children.empty(), "the paragraph beside the float has a text node");
        bool saw_fragment_below_the_float = false;
        for (auto const& fragment : para.children[0]->text_fragments) {
            if (fragment.rect.y < float_bottom - 0.5f)
                require(fragment.rect.x + fragment.rect.width <= float_left + 0.5f,
                    "a word that does not fit beside a float drops past it instead of overflowing into it");
            else
                saw_fragment_below_the_float = true;
        }
        require(saw_fragment_below_the_float,
            "the word that does not fit beside the float is placed below it");
    }

    return 0;
}
