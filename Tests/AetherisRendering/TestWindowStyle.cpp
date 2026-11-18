#include "WindowTestHarness.hpp"

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

    // Incremental layout: an attribute write that can only change paint must rebuild
    // the render tree without running the layout engine. The summary reports both
    // counts, so "laid out" and "repainted" are told apart from outside the process.
    {
        // Background only, written on every tick: six relayouts, one layout - the one
        // the page loaded with.
        auto paint = run_window(binary,
            "<html><head><title>Incremental</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{background:#ff00ff}"
            "</style></head><body><div id='a'>a</div><script>"
            "setInterval(function () { document.getElementById('a').setAttribute('class', 'on'); }, 50);"
            "</script></body></html>", "", 20);
        require(paint.status == 0, "the paint-only run exits cleanly");
        require(summary_count(paint.output, ", relayouts: ") == 6,
            "a background-only class write relayouts on every tick");
        require(summary_count(paint.output, ", layouts: ") == 1,
            "a background-only class write never runs the layout engine");

        // A text write can reflow, so every relayout of one runs the engine. This is
        // the other direction of the same claim, and it must be able to come out the
        // other way or the first assertion proves nothing.
        auto text = run_window(binary,
            "<html><head><title>Incremental text</title></head><body><div id='n'>0</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').textContent = String(n); }, 50);"
            "</script></body></html>", "", 20);
        require(text.status == 0, "the text-write run exits cleanly");
        require(summary_count(text.output, ", relayouts: ") == 6,
            "a text write relayouts on every tick");
        require(summary_count(text.output, ", layouts: ") == summary_count(text.output, ", relayouts: ") + 1,
            "a text write runs the layout engine on every relayout");

        // The control: the same attribute write, changing width instead of background.
        // If this also skipped, the skip would be unconditional and the two runs above
        // would agree by accident.
        auto width = run_window(binary,
            "<html><head><title>Incremental width</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{width:200px}"
            "</style></head><body><div id='a'>a</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('a').setAttribute('class', n === 1 ? 'on' : ''); }, 50);"
            "</script></body></html>", "", 20);
        require(width.status == 0, "the width-change run exits cleanly");
        require(summary_count(width.output, ", relayouts: ") == 6,
            "a width-changing class write relayouts on every tick");
        require(summary_count(width.output, ", layouts: ") > 1,
            "a class write that changes width does run the layout engine");

        // And the skipped layout must not skip the paint. The frame is read, not
        // inferred: a stale frame after a skipped layout is the bug this project has
        // shipped twice.
        // Under the build tree, not /tmp: this sandbox's /tmp is a 64 MB tmpfs and the
        // suite's other frame dumps already fill it, which truncated these frames to
        // zero bytes and failed this assertion for a reason that had nothing to do with
        // layout. The frame is 1024x768x3, so eight of them are 19 MB.
        std::string directory = "aetheris-incremental-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto repaint = run_window(binary,
            "<html><head><title>Incremental paint</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div><script>"
            "setTimeout(function () { document.getElementById('a').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2, directory);
        require(repaint.status == 0, "the skipped-layout repaint run exits cleanly");
        require(summary_count(repaint.output, ", relayouts: ") == 1,
            "the paint-only change relayouts once");
        require(summary_count(repaint.output, ", layouts: ") == 1,
            "and does not run the layout engine");
        int painted[3] = { 0, 0, 0 };
        int outside[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, painted), "the repaint pixel is readable");
        require(frame_pixel(directory, 500, 34 + 50, outside), "the control pixel is readable");
        require(is_magenta(painted), "a skipped layout still repaints the new background");
        require(!is_magenta(outside), "and the paint stops where the box does");
        std::system(("rm -rf " + directory).c_str());
    }

    // The layout key is a deny-list of paint-only properties, so a property the engine reads and
    // nobody classified still reflows. It used to be an allow-list, and it was missing `gap`,
    // `row-gap`, `column-gap`, `flex-direction`, `flex-grow`, `align-items`, `justify-content`,
    // `grid-template-columns` and `aspect-ratio` - all read by `layout.cpp`. Found by auditing
    // the list against what the engine reads, not by a test, which is why this one exists.
    {
        std::string directory = "aetheris-layout-key-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto gap = run_window(binary,
            "<html><head><title>Layout key gap</title><style>body{margin:0}"
            "#box{display:flex;width:400px;height:100px;background:#0000ff}"
            ".a{width:50px;height:100px;background:#ff00ff}"
            ".b{width:50px;height:100px;background:#00ff00}"
            "#box.on{gap:200px}</style></head>"
            "<body><div id='box'><div class='a'>a</div><div class='b'>b</div></div><script>"
            "setTimeout(function () { document.getElementById('box').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2, directory);
        require(gap.status == 0, "the gap run exits cleanly");
        require(summary_count(gap.output, ", relayouts: ") == 1, "a gap change relayouts once");
        require(summary_count(gap.output, ", layouts: ") == 2,
            "a class change that only moves \"gap\" runs the layout engine");
        int moved[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 250, 34 + 50, moved), "the second box pixel is readable");
        require(is_green(moved), "the box moved, so the frame is not stale");

        // The other direction: a colour change is paint-only and must still skip the pass.
        auto colour = run_window(binary,
            "<html><head><title>Layout key colour</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff;color:#ffffff}.on{color:#000000}"
            "</style></head><body><div id='a'>a</div><script>"
            "setTimeout(function () { document.getElementById('a').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2);
        require(colour.status == 0, "the colour run exits cleanly");
        require(summary_count(colour.output, ", layouts: ") == 1,
            "a class change that only changes colour does not run the layout engine");
        std::system(("rm -rf " + directory).c_str());
    }

    // Per-node layout: a subtree nothing restyled and that sits where it sat is copied from the
    // last pass instead of laid out. `layout nodes:` is the count that says so.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'" + std::string(index == 100 ? " id='mid'" : "") + ">row "
                + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Per-node layout</title><style>body{margin:0}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "#mid{background:#0000ff}"
                                 "#first{width:400px;height:3px;background:#0000ff}"
                                 "#first.tall{height:60px}";

        // A text edit in row 100 that changes nothing else: the chain down to it is laid out,
        // the other 400 nodes are not.
        auto edit = run_window(binary,
            page + "</style></head><body>" + rows + "<script>"
                   "setTimeout(function () { document.getElementById('mid').textContent = 'ROW 100'; }, 5);"
                   "</script></body></html>",
            "", 2);
        require(edit.status == 0, "the per-node text edit run exits cleanly");
        require(summary_count(edit.output, ", relayouts: ") == 1, "the text edit relayouts once");
        int laid_out = summary_count(edit.output, ", layout nodes: ");
        require(laid_out > 0 && laid_out < 40,
            "a text edit in row 100 of 200 lays out the chain, not the page");

        // The control: growing the first row moves every row below it, so they cannot be reused
        // where they were and the pass must lay them out.
        std::string const directory = "aetheris-per-node-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto grow = run_window(binary,
            page + "</style></head><body><div id='first'>first</div>" + rows + "<script>"
                   "setTimeout(function () { document.getElementById('first').setAttribute('class', 'tall'); }, 5);"
                   "</script></body></html>",
            "", 2, directory);
        require(grow.status == 0, "the growing-row run exits cleanly");
        int moved = summary_count(grow.output, ", layout nodes: ");
        require(moved > 200,
            "rows that moved are laid out again, not reused where they used to be");
        // Content y 1 is inside the 60 px first row after the change; content y 70 is the first
        // row that follows it. Both were read off the frame before this assertion was written.
        int inside_first[3] = { 0, 0, 0 };
        int after_first[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 1, inside_first), "the first-row pixel is readable");
        require(frame_pixel(directory, 200, 34 + 70, after_first), "the row-after pixel is readable");
        require(is_blue(inside_first), "the first row grew to 60 px");
        require(is_green(after_first), "and the rows below moved down with it");
        std::system(("rm -rf " + directory).c_str());
    }

    // element.focus() and element.blur(). The request is recorded during script execution and performed
    // by the loop with set_focus(), the same helper a click or a Tab key uses, so the state moves and
    // `:focus` re-resolves.
    //
    // What this asserts is the state, not the events: a listener registered with
    // addEventListener('focus', ...) does not run (see CHECKLIST.md, open defects).
    {
        std::string const directory = "aetheris-focus-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Focus</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:focus{background:#ff00ff}"
            "</style></head><body><input id='a'><input id='b'><script>"
            "setTimeout(function () { document.getElementById('a').focus(); }, 5);"
            "</script></body></html>", "", 3, directory);
        require(result.status == 0, "the focus run exits cleanly");
        int focused[3] = { 0, 0, 0 };
        int other[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 5, focused), "the focused control is readable");
        require(frame_pixel(directory, 50, 34 + 25, other), "the unfocused control is readable");
        require(is_magenta(focused), "element.focus() moves the focus state");
        require(is_green(other), "focusing one control does not focus its sibling");

        // blur() clears it again.
        auto blurred = run_window(binary,
            "<html><head><title>Blur</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:focus{background:#ff00ff}"
            "</style></head><body><input id='a'><script>"
            "setTimeout(function () { var a = document.getElementById('a'); a.focus(); a.blur(); }, 5);"
            "</script></body></html>", "", 3, directory);
        require(blurred.status == 0, "the blur run exits cleanly");
        int after[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 5, after), "the blurred control is readable");
        require(is_green(after), "element.blur() clears the focus state");
        std::system(("rm -rf " + directory).c_str());
    }

    // Soft-wrap opportunities: an inline item breaks only where whitespace precedes it. Upstream:
    // inline_formatting_context.rs, text_item_wrap_opportunity() - a non-whitespace item may break only
    // when the line ends in whitespace or the item allows a break before it.
    //
    // Two fixtures that differ in one character: the space between the spans.
    {
        auto probe_pair = [&](std::string const& between, char const* what, bool expect_break) {
            auto result = run_window(binary,
                "<html><head><title>Wrap opportunity</title><style>body{margin:0;font-size:16px}"
                "div{width:100px}</style></head><body><div>"
                "<span id='s1'>aaaaaa</span>" + between + "<span id='s2'>bbbbbb</span></div><script>"
                "setTimeout(function () {"
                "var a = document.getElementById('s1').getBoundingClientRect();"
                "var b = document.getElementById('s2').getBoundingClientRect();"
                "console.log('MARK same-line ' + (a.y === b.y));"
                "}, 5);</script></body></html>", "", 3);
            require(result.status == 0, what);
            std::string const expected = expect_break ? "MARK same-line false" : "MARK same-line true";
            require(result.output.find(expected) != std::string::npos, what);
        };
        probe_pair(" ", "a space between two inline items lets the second wrap", true);
        probe_pair("", "no space between two inline items keeps them together", false);
    }

    // Inline wrapping at the container edge. Upstream decides a break on a soft-wrap opportunity
    // (Rust/src/layout/line_box.rs, `has_soft_wrap_opportunity_after`); this engine's flow path has a
    // simpler rule - an inline item that does not fit starts a new line - and that rule only became
    // reachable once inline boxes were content-sized.
    {
        std::string spans;
        for (int index = 0; index < 8; ++index) {
            auto const two_digits = std::string(index < 10 ? "0" : "") + std::to_string(index);
            spans += "<span id='w" + std::to_string(index) + "'>" + two_digits + "</span>";
        }
        auto result = run_window(binary,
            "<html><head><title>Inline wrap</title><style>body{margin:0;font-size:16px}"
            "#box{width:100px}</style></head><body><div id='box'>" + spans + "</div><script>"
            "setTimeout(function () { var ys = [];"
            "for (var i = 0; i < 8; ++i) ys.push(document.getElementById('w' + i).getBoundingClientRect().y);"
            "console.log('MARK ys ' + ys.join(','));"
            "}, 5);</script></body></html>", "", 3);
        require(result.status == 0, "the inline wrap run exits cleanly");
        auto const at = result.output.find("MARK ys ");
        require(at != std::string::npos, "the wrap run reports its line positions");
        // Two distinct y values, non-decreasing: the spans wrapped once and did not stack one per line.
        std::string const values = result.output.substr(at + 8, result.output.find('\n', at) - (at + 8));
        std::vector<float> ys;
        std::string current;
        for (char const c : values) {
            if (c == ',') {
                if (!current.empty())
                    ys.push_back(std::stof(current));
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty())
            ys.push_back(std::stof(current));
        require(ys.size() == 8, "every span reports a position");
        std::vector<float> distinct;
        for (float const y : ys)
            if (distinct.empty() || std::abs(distinct.back() - y) > 0.5f)
                distinct.push_back(y);
        require(distinct.size() == 2, "eight spans in a 100px box wrap onto two lines");
        for (std::size_t index = 1; index < ys.size(); ++index)
            require(ys[index] >= ys[index - 1], "line positions never go backwards");
    }

    // Inline siblings flow onto one line, and an inline box is as wide as its content. Three spans
    // used to be three full-width lines at x = 0, because a text run's box was given the whole
    // remaining line width - which poisoned every inline ancestor's shrink-to-fit.
    {
        auto result = run_window(binary,
            "<html><head><title>Inline flow</title><style>body{margin:0;font-size:16px}</style>"
            "</head><body><div><span id='s1'>aa</span><span id='s2'>bb</span><span id='s3'>cc</span></div>"
            "<script>setTimeout(function () {"
            "var a = document.getElementById('s1').getBoundingClientRect();"
            "var b = document.getElementById('s2').getBoundingClientRect();"
            "var c = document.getElementById('s3').getBoundingClientRect();"
            "console.log('MARK y ' + a.y + ' ' + b.y + ' ' + c.y);"
            "console.log('MARK x ' + (b.x > a.x && c.x > b.x));"
            "console.log('MARK widths ' + (a.width < 100 && b.width < 100 && c.width < 100));"
            "}, 5);</script></body></html>", "", 3);
        require(result.status == 0, "the inline flow run exits cleanly");
        require(result.output.find("MARK y 0 0 0") != std::string::npos, "three spans share one line");
        require(result.output.find("MARK x true") != std::string::npos, "each span starts where the last ended");
        require(result.output.find("MARK widths true") != std::string::npos,
            "an inline box is as wide as its content, not as wide as the line");
    }

    // `visibility`. Upstream: inherited (CSS/Properties.json), initial `visible`, affects hit testing,
    // and paint checks it per box (Painting/BoxViews.cpp:189). The difference from display:none is the
    // whole point - the box still occupies space, and a descendant that re-sets `visible` is painted
    // inside a hidden ancestor.
    {
        std::string const directory = "aetheris-visibility-frames";
        // 1. Not painted, but still taking space.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Visibility</title><style>body{margin:0}"
            "div{width:200px;height:20px;background:#00ff00}#h{visibility:hidden;background:#ff00ff}"
            "</style></head><body><div id='h'></div><div id='after'></div></body></html>", "", 2, directory);
        require(result.status == 0, "the visibility run exits cleanly");
        int hidden[3] = { 0, 0, 0 };
        int after[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 5, hidden), "the hidden pixel is readable");
        require(frame_pixel(directory, 50, 34 + 25, after), "the pixel after it is readable");
        require(!is_magenta(hidden) && !is_green(hidden), "a visibility:hidden element paints nothing");
        require(is_green(after), "a visibility:hidden element still takes its space in the flow");

        // 2. A child that re-sets `visible` inside a hidden parent is painted.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto nested = run_window(binary,
            "<html><head><title>Visibility child</title><style>body{margin:0}"
            "#h{visibility:hidden;width:200px;height:40px;background:#ff00ff}"
            "#c{visibility:visible;width:100px;height:20px;background:#0000ff}"
            "</style></head><body><div id='h'><div id='c'></div></div></body></html>", "", 2, directory);
        require(nested.status == 0, "the nested visibility run exits cleanly");
        int child[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 5, child), "the child pixel is readable");
        require(child[0] == 0 && child[1] == 0 && child[2] == 255,
            "a child that sets visibility:visible is painted inside a hidden parent");

        // 3. The third clause of focusability.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto focusable = run_window(binary,
            "<html><head><title>Visibility focus</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:focus{background:#ff00ff}#t{visibility:hidden}"
            "</style></head><body><input id='t'><script>"
            "setTimeout(function () { document.getElementById('t').focus(); }, 5);"
            "</script></body></html>", "", 3, directory);
        require(focusable.status == 0, "the visibility focus run exits cleanly");
        int focused[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 5, focused), "the visibility focus pixel is readable");
        require(!is_magenta(focused), "a visibility:hidden control cannot be focused");
        std::system(("rm -rf " + directory).c_str());
    }

    // A click focuses the nearest focusable ancestor, and moves nothing if there is none. Upstream's
    // focus_candidate_for_position() (Page/EventHandler.cpp:3543) walks up while the node is not
    // focusable and then runs the focusing steps with a null fallback, which returns without touching
    // focus. A plain div is therefore not a focus target, and a span inside a button focuses the button.
    {
        std::string const directory = "aetheris-click-focus-frames";
        struct ClickCase {
            char const* what;
            std::string body;
            bool focused;
        };
        std::vector<ClickCase> const cases {
            { "a click on a plain div focuses nothing", "<div id='t'>x</div>", false },
            { "a click on an input focuses it", "<input id='t'>", true },
            { "a click on a span inside a button focuses the button",
                "<button><span id='t'>x</span></button>", true },
            { "a click on a span inside a plain div focuses nothing",
                "<div><span id='t'>x</span></div>", false },
            { "a click on a div with tabindex focuses it", "<div id='t' tabindex='0'>x</div>", true },
            { "a click on a disabled button focuses nothing", "<button id='t' disabled>x</button>", false },
        };
        for (auto const& click_case : cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            std::string const html = "<html><head><title>Click focus</title><style>body{margin:0}"
                "div,input,button,a{display:block;margin:0;padding:0;border:0;width:400px;height:20px;"
                "background:#00ff00}:focus{background:#ff00ff}"
                "</style></head><body>" + click_case.body + "</body></html>";
            auto result = run_window(binary, html, "--click 200,44", 3, directory);
            std::string const message = std::string("the click-focus run for ") + click_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 44, rgb), "the click-focus pixel is readable");
            require(click_case.focused ? is_magenta(rgb) : !is_magenta(rgb), click_case.what);
        }
        std::system(("rm -rf " + directory).c_str());
    }

    // focus() refuses a target that is not focusable, which is upstream's first move in the focusing
    // steps (HTML/Focus.cpp:749 -> get_focusable_area). The rules, read from three levels:
    //   Element::is_focusable()          tabindex parses as an integer
    //   HTMLAnchorElement::is_focusable  base || has_attribute(href)
    //   HTMLInputElement::is_focusable   type != hidden && enabled()  (button/select/textarea: enabled())
    //
    // One candidate per page: eight focus() calls in one timer all run inside one run_timers() window,
    // so only the last would be visible and every earlier one would read as a failure.
    {
        std::string const directory = "aetheris-focusable-frames";
        struct FocusableCase {
            char const* what;
            std::string body;
            bool focusable;
        };
        std::vector<FocusableCase> const cases {
            { "a div with no tabindex is not focusable", "<div id='t'>x</div>", false },
            { "a div with tabindex is focusable", "<div id='t' tabindex='0'>x</div>", true },
            { "an input is focusable", "<input id='t'>", true },
            { "an input of type hidden is not", "<input id='t' type='hidden'>", false },
            { "a disabled button is not", "<button id='t' disabled>x</button>", false },
            { "an anchor without href is not", "<a id='t'>x</a>", false },
            { "an anchor with href is", "<a id='t' href='/x'>x</a>", true },
            // The rendering requirement: upstream's meets_focusable_area_rendering_requirements()
            // refuses a display:none subtree. We ask the layout node and refuse only what it can prove
            // is hidden, so a focus() before the first layout is still allowed - which the last case
            // checks, because a page that focuses on load must not be refused.
            { "an input with display:none is not", "<input id='t' style='display:none'>", false },
            { "an input inside a display:none parent is not",
                "<div style='display:none'><input id='t'></div>", false },
            { "focus() during load on a visible input is allowed", "<input id='t'>", true },
        };
        for (auto const& focus_case : cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            std::string const html = "<html><head><title>Focusable</title><style>body{margin:0}"
                "div,button,input,a{display:block;margin:0;padding:0;border:0;width:100px;height:20px;"
                "background:#00ff00}:focus{background:#ff00ff}"
                "</style></head><body>" + focus_case.body +
                // The last case focuses during load rather than after a timer, which is what proves the
                // check does not require a layout node that does not exist yet.
                + (std::string(focus_case.what).find("during load") != std::string::npos
                        ? "<script>document.getElementById('t').focus();</script>"
                        : "<script>setTimeout(function () { document.getElementById('t').focus(); }, 5);</script>")
                + "</body></html>";
            auto result = run_window(binary, html, "", 3, directory);
            std::string const message = std::string("the focusable run for ") + focus_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + 5, rgb), "the focusable pixel is readable");
            // "Not focusable" is asserted as "not magenta", not as "green": a display:none element
            // renders nothing, so its pixel is the page background. The first version of this expected
            // green everywhere and failed on a correct engine.
            require(focus_case.focusable ? is_magenta(rgb) : !is_magenta(rgb), focus_case.what);
        }
        std::system(("rm -rf " + directory).c_str());
    }

    // The URL components `location` exposes: origin, pathname, search, hash. Upstream serialises them
    // from the parsed URL record (HTML/Location.idl); this splits the string the embedder hands back.
    {
        auto result = run_window(binary,
            "<html><head><title>Location parts</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK parts ' + location.pathname + '|' + location.search + '|' + location.hash);"
            "console.log('MARK origin ' + (location.origin.indexOf('127.0.0.1') !== -1));"
            "</script></body></html>", "", 2);
        require(result.status == 0, "the location-parts run exits cleanly");
        // The harness serves the document at "/", so the path is "/" and there is no query or fragment.
        require(result.output.find("MARK parts /||") != std::string::npos,
            "pathname, search and hash are the three components, empty where absent");
        require(result.output.find("MARK origin true") != std::string::npos,
            "origin is the scheme and host, without the path");
    }

    // A generated box paints its text. Upstream's generated node carries its text and its generator

    cleanup_frames();
    return 0;
}
