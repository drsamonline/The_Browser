#include "WindowTestHarness.hpp"

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

    // asks for - a geometry API checked by arithmetic the page does, not by a pixel someone eyeballed.
    //
    // Read after a timer: a script that runs at load has no layout to measure yet, and upstream
    // returns an empty rect there too. There is no forced layout read here, which is the one place
    // this differs from upstream's ForcedReadScope.
    {
        auto result = run_window(binary,
            "<html><head><title>Rect</title><style>body{margin:0}"
            "#a{width:200px;height:50px;background:#0000ff}"
            "#b{width:100px;height:20px;margin-left:30px;background:#ff00ff}"
            "</style></head><body><div id='a'></div><div id='b'></div><script>"
            "setTimeout(function () {"
            "var a = document.getElementById('a').getBoundingClientRect();"
            "var b = document.getElementById('b').getBoundingClientRect();"
            "console.log('MARK a ' + a.x + ' ' + a.y + ' ' + a.width + ' ' + a.height);"
            "console.log('MARK corners ' + a.right + ' ' + a.bottom);"
            "console.log('MARK b ' + b.x + ' ' + b.y + ' ' + b.width + ' ' + b.height);"
            "}, 5);</script></body></html>", "", 3);
        require(result.status == 0, "the getBoundingClientRect run exits cleanly");
        require(result.output.find("MARK a 0 0 200 50") != std::string::npos,
            "getBoundingClientRect reports the box's position and size");
        require(result.output.find("MARK corners 200 50") != std::string::npos,
            "right and bottom are the box's far edges");
        require(result.output.find("MARK b 30 50 100 20") != std::string::npos,
            "a margin and a preceding block are both in the coordinates");
    }

    // Sibling combinators, the attribute case flag and the form states. Upstream matches `+` by
    // stepping to tree.previous_sibling() and `~` by walking back over earlier siblings
    // (Rust/src/css/style/component_matching.rs); the attribute flag threads through every match
    // type as `equals(value, literal, insensitive)`.
    //
    // The form fixtures set `border:0;margin:0;padding:0`: an input's user-agent border made a fixed
    // 20 px probe step land on the previous input, which read as a matcher bug twice before the
    // geometry was checked.
    {
        std::string const directory = "aetheris-batch-a-frames";
        auto stripe = [](std::string const& rule) {
            std::string rows;
            for (int index = 0; index < 4; ++index)
                rows += "<div class='r' id='r" + std::to_string(index) + "'>r</div>";
            return "<html><head><title>Batch A</title><style>body{margin:0}"
                   ".r{width:400px;height:10px;background:#00ff00}" + rule +
                   "</style></head><body>" + rows + "</body></html>";
        };
        struct StripeCase {
            char const* what;
            std::string html;
            int expected[4][3];
        };
        std::vector<StripeCase> const stripe_cases {
            { "a + b matches only the element after b",
                stripe("#r1 + .r{background:#ff00ff}"),
                { { 0, 255, 0 }, { 0, 255, 0 }, { 255, 0, 255 }, { 0, 255, 0 } } },
            { "a ~ b matches every following sibling",
                stripe("#r1 ~ .r{background:#ff00ff}"),
                { { 0, 255, 0 }, { 0, 255, 0 }, { 255, 0, 255 }, { 255, 0, 255 } } },
        };
        for (auto const& stripe_case : stripe_cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto result = run_window(binary, stripe_case.html, "", 2, directory);
            std::string const message = std::string("the sibling run for ") + stripe_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            for (int row = 0; row < 4; ++row) {
                int rgb[3] = { 0, 0, 0 };
                require(frame_pixel(directory, 200, 34 + row * 10 + 5, rgb), "a sibling pixel is readable");
                require(rgb[0] == stripe_case.expected[row][0] && rgb[1] == stripe_case.expected[row][1]
                        && rgb[2] == stripe_case.expected[row][2],
                    stripe_case.what);
            }
        }

        // The attribute case flag, both directions, and the default staying case-sensitive. The space
        // inside `[a=v i]` also proved the scanners need brackets as well as parentheses.
        struct AttrCase {
            char const* what;
            std::string rule;
            bool first_matches;
            bool second_matches;
        };
        std::vector<AttrCase> const attr_cases {
            { "[data-mode=LIST i] matches 'list' and not 'grid'", "div[data-mode=LIST i]{background:#ff00ff}", true, false },
            // Without the flag, LIST and list are different values: this must not match either div.
            { "[data-mode=LIST] stays case-sensitive", "div[data-mode=LIST]{background:#ff00ff}", false, false },
        };
        for (auto const& attr_case : attr_cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            std::string const html = "<html><head><title>Attr</title><style>body{margin:0}"
                "div{width:400px;height:10px;background:#00ff00}" + attr_case.rule +
                "</style></head><body><div data-mode='list'>a</div><div data-mode='grid'>b</div></body></html>";
            auto result = run_window(binary, html, "", 2, directory);
            std::string const message = std::string("the attribute run for ") + attr_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int first[3] = { 0, 0, 0 };
            int second[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + 5, first), "the attribute pixel is readable");
            require(frame_pixel(directory, 200, 34 + 15, second), "the second attribute pixel is readable");
            require(attr_case.first_matches ? is_magenta(first) : is_green(first), attr_case.what);
            require(attr_case.second_matches ? is_magenta(second) : is_green(second), attr_case.what);
        }

        // Form states: three controls, one of each state, borderless so the probe steps land on them.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto forms = run_window(binary,
            "<html><head><title>Forms</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:checked{background:#ff00ff}input:disabled{background:#0000ff}"
            "input:placeholder-shown{background:#00ffff}"
            "</style></head><body>"
            "<input checked><input disabled><input placeholder='x' value=''><input placeholder='x' value='f'>"
            "</body></html>", "", 2, directory);
        require(forms.status == 0, "the form-state run exits cleanly");
        int const form_expected[4][3] { { 255, 0, 255 }, { 0, 0, 255 }, { 0, 255, 255 }, { 0, 255, 0 } };
        for (int row = 0; row < 4; ++row) {
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + row * 20 + 5, rgb), "a form pixel is readable");
            require(rgb[0] == form_expected[row][0] && rgb[1] == form_expected[row][1]
                    && rgb[2] == form_expected[row][2],
                ":checked, :disabled and :placeholder-shown each style their own control");
        }
        std::system(("rm -rf " + directory).c_str());
    }

    // Custom properties and var(). Upstream resolves them at style-resolution time against an
    // inherited store with cycle tracking (Rust/src/css/custom_properties.rs, resolve_vars); here the
    // store is the same property map, the inheritance is a prefix copy, and the cycle guard is a
    // depth limit.
    {
        std::string const directory = "aetheris-var-frames";
        struct VarCase {
            char const* what;
            std::string css;
            std::string body;
            int expected[3];
        };
        std::vector<VarCase> const cases {
            { "a var() reads a custom property from :root",
                ":root{--c:#ff00ff}.box{width:400px;height:100px;background:var(--c)}",
                "<div class='box'></div>", { 255, 0, 255 } },
            { "an unresolvable var() takes its fallback",
                ".box{width:400px;height:100px;background:var(--missing, #00ff00)}",
                "<div class='box'></div>", { 0, 255, 0 } },
            { "a var() chain resolves through two properties",
                ":root{--a:#ff00ff;--b:var(--a)}.box{width:400px;height:100px;background:var(--b)}",
                "<div class='box'></div>", { 255, 0, 255 } },
            { "an inline style substitutes an inherited custom property",
                ":root{--c:#ff00ff}.box{width:400px;height:100px;background:#0000ff}",
                "<div class='box' style='background:var(--c)'></div>", { 255, 0, 255 } },
            { "a var() reads a custom property two elements up",
                ":root{--c:#ff00ff}#outer{width:400px;height:100px;background:#0000ff}"
                "#outer .box{background:var(--c)}",
                "<div id='outer'><div class='box' style='height:100px'></div></div>", { 255, 0, 255 } },
        };
        for (auto const& var_case : cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            std::string const html = "<html><head><title>Var</title><style>body{margin:0}"
                + var_case.css + "</style></head><body>" + var_case.body + "</body></html>";
            auto result = run_window(binary, html, "", 2, directory);
            std::string const message = std::string("the var run for ") + var_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + 50, rgb), "the var pixel is readable");
            require(rgb[0] == var_case.expected[0] && rgb[1] == var_case.expected[1]
                    && rgb[2] == var_case.expected[2],
                var_case.what);
        }

        // A cycle must terminate and must not paint the wrong colour. It exits 0 with no substitution,
        // so the declaration is dropped and the box keeps the default background.
        auto cycle = run_window(binary,
            "<html><head><title>Var cycle</title><style>body{margin:0}"
            ":root{--a:var(--b);--b:var(--a)}.box{width:400px;height:100px;background:var(--a)}"
            "</style></head><body><div class='box'></div></body></html>", "", 2, directory);
        require(cycle.status == 0, "a var() cycle terminates instead of recursing forever");
        std::system(("rm -rf " + directory).c_str());
    }

    // The structural pseudo-class set. Each rule is asserted on a row it must match and on rows it
    // must not, so every assertion can come out the other way.
    {
        std::string const directory = "aetheris-pseudo-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Pseudo classes</title><style>body{margin:0}"
            "div{width:400px;height:10px;background:#00ff00}"
            "span{display:block;width:400px;height:10px;background:#00ff00}"
            "div:first-of-type{background:#ff00ff}"
            "div:nth-of-type(2){background:#00ffff}"
            "div:last-of-type{background:#0000ff}"
            "span:only-of-type{background:#ffff00}"
            "</style></head><body><div>a</div><div>b</div><div>c</div><span>s</span>"
            "</body></html>", "", 2, directory);
        require(result.status == 0, "the pseudo-class run exits cleanly");
        struct Row {
            char const* what;
            int y;
            int colour[3];
        };
        Row const rows[] {
            { "div:first-of-type matches the first div", 5, { 255, 0, 255 } },
            { "div:nth-of-type(2) matches the second div", 15, { 0, 255, 255 } },
            { "div:last-of-type matches the third div", 25, { 0, 0, 255 } },
            { "span:only-of-type matches the only span", 35, { 255, 255, 0 } },
        };
        for (auto const& row : rows) {
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + row.y, rgb), "a pseudo-class pixel is readable");
            require(rgb[0] == row.colour[0] && rgb[1] == row.colour[1] && rgb[2] == row.colour[2], row.what);
        }

        // An+B notation: the zebra-stripe rule every second list uses, and a form that only a
        // parsed step can answer. Before this, `:nth-child(2n)` matched nothing at all.
        auto stripes = run_window(binary,
            "<html><head><title>Stripes</title><style>body{margin:0}"
            "div{width:400px;height:10px;background:#00ff00}div:nth-child(2n){background:#ff00ff}"
            "div:nth-child(3n+1){background:#0000ff}"
            "</style></head><body><div>1</div><div>2</div><div>3</div><div>4</div><div>5</div><div>6</div>"
            "</body></html>", "", 2, directory);
        require(stripes.status == 0, "the An+B run exits cleanly");
        // 3n+1 wins on rows 1 and 4, 2n on rows 2 and 6, and 3 and 5 are only 2n's business - so the
        // expected colours are blue, magenta, green, blue, green, magenta.
        int const expected[6][3] { { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 0 },
                                   { 0, 0, 255 }, { 0, 255, 0 }, { 255, 0, 255 } };
        for (int row = 0; row < 6; ++row) {
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + row * 10 + 5, rgb), "a stripe pixel is readable");
            require(rgb[0] == expected[row][0] && rgb[1] == expected[row][1] && rgb[2] == expected[row][2],
                "nth-child(2n) and nth-child(3n+1) stripe the list as written");
        }

        // :is(), :where() and a list for :not(). Upstream matches all three over a selector list
        // (Rust/src/css/style/component_matching.rs) and scores them as
        // `greatest(arguments)` / zero / `greatest(arguments)` (selector_arena.rs).
        //
        // Two splitters had to learn about parentheses first: the rule parser split on every comma
        // (`:is(#a, #b)` became `:is(#a` and `#b)`), and the combinator splitter read a space inside
        // `:is(...)` as a descendant combinator. Both were found by these pixels.
        auto stripe_page = [](std::string const& rule) {
            std::string rows;
            for (int index = 0; index < 6; ++index)
                rows += "<div class='r' id='r" + std::to_string(index) + "'>r</div>";
            return "<html><head><title>Is</title><style>body{margin:0}"
                   "div.r{width:400px;height:10px;background:#00ff00}#r1{background:#0000ff}"
                + rule + "</style></head><body>" + rows + "</body></html>";
        };
        struct IsCase {
            char const* what;
            std::string html;
            int expected[6][3];
        };
        std::vector<IsCase> const is_cases {
            // Row 2 is #r1's blue: every page carries that rule so the :where case has something
            // to lose to, and :is(#r0, #r2, #r4) does not touch it.
            { ":is(#r0, #r2, #r4) matches three rows",
                stripe_page(":is(#r0, #r2, #r4){background:#ff00ff}"),
                { { 255, 0, 255 }, { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 0 }, { 255, 0, 255 }, { 0, 255, 0 } } },
            { ":where() contributes no specificity, so it loses to both rules",
                stripe_page(":where(.r){background:#ff00ff}"),
                { { 0, 255, 0 }, { 0, 0, 255 }, { 0, 255, 0 }, { 0, 255, 0 }, { 0, 255, 0 }, { 0, 255, 0 } } },
            { ":is() takes the greatest argument, so it ties with #r1 and wins on order",
                stripe_page(":is(.r, #r1){background:#ff00ff}"),
                { { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 } } },
            { ":not() takes a list and matches neither",
                stripe_page("div:not(#r0, #r1){background:#ff00ff}"),
                { { 0, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 }, { 255, 0, 255 } } },
        };
        for (auto const& is_case : is_cases) {
            auto result = run_window(binary, is_case.html, "", 2, directory);
            std::string const message = std::string("the :is run for ") + is_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            for (int row = 0; row < 6; ++row) {
                int rgb[3] = { 0, 0, 0 };
                require(frame_pixel(directory, 200, 34 + row * 10 + 5, rgb), "an :is pixel is readable");
                require(rgb[0] == is_case.expected[row][0] && rgb[1] == is_case.expected[row][1]
                        && rgb[2] == is_case.expected[row][2],
                    is_case.what);
            }
        }

        // The sibling forms of :has(), and the invalidation they need. A sibling combinator inside
    // parentheses is still a sibling dependency, so uses_sibling_sequence() scans at any depth, and an
    // attribute change now dirties the changed node's siblings as well as its ancestors.
    {
        std::string const directory = "aetheris-has-sibling-frames";
        struct HasSiblingCase {
            char const* what;
            std::string rule;
            std::string body;
            int expected[4][3];
        };
        auto rows = [](std::string const& extra_at) {
            std::string out;
            for (int index = 0; index < 4; ++index) {
                std::string classes = "r";
                if (index == 1 && extra_at == "second") classes += " x";
                if (index == 2 && extra_at == "third") classes += " x";
                out += "<div class='" + classes + "' id='r" + std::to_string(index) + "'></div>";
            }
            return out;
        };
        std::vector<HasSiblingCase> const cases {
            // The `.x` row is 10px wide, so at x=200 it is the page background, not its own colour.
            { ":has(+ .x) styles the row immediately before it",
                ".r:has(+ .x){background:#ff00ff}", rows("second"),
                { { 255, 0, 255 }, { 255, 255, 255 }, { 0, 255, 0 }, { 0, 255, 0 } } },
            { ":has(~ .x) styles every row before it",
                ".r:has(~ .x){background:#ff00ff}", rows("third"),
                { { 255, 0, 255 }, { 255, 0, 255 }, { 255, 255, 255 }, { 0, 255, 0 } } },
            { ":has(+ .nope) matches nothing",
                ".r:has(+ .nope){background:#ff00ff}", rows("none"),
                { { 0, 255, 0 }, { 0, 255, 0 }, { 0, 255, 0 }, { 0, 255, 0 } } },
        };
        for (auto const& has_case : cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            std::string const html = "<html><head><title>Has sibling</title><style>body{margin:0}"
                ".r{width:400px;height:10px;background:#00ff00}.x{width:10px;height:10px;background:#0000ff}"
                + has_case.rule + "</style></head><body>" + has_case.body + "</body></html>";
            auto result = run_window(binary, html, "", 2, directory);
            std::string const message = std::string("the :has sibling run for ") + has_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            for (int row = 0; row < 4; ++row) {
                int rgb[3] = { 0, 0, 0 };
                require(frame_pixel(directory, 200, 34 + row * 10 + 5, rgb), "a :has sibling pixel is readable");
                require(rgb[0] == has_case.expected[row][0] && rgb[1] == has_case.expected[row][1]
                        && rgb[2] == has_case.expected[row][2],
                    has_case.what);
            }
        }

        // The invalidation: a following sibling gaining the class must restyle the row before it.
        // Without the sibling dirty this stayed green, which is the stale style the row exists for.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto mutated = run_window(binary,
            "<html><head><title>Has sibling mutation</title><style>body{margin:0}"
            ".r{width:400px;height:10px;background:#00ff00}.x{width:10px;height:10px;background:#0000ff}"
            ".r:has(+ .x){background:#ff00ff}"
            "</style></head><body>"
            "<div class='r' id='r0'></div><div class='r' id='r1'></div><div class='r' id='r2'></div>"
            "<script>setTimeout(function () { document.getElementById('r1').classList.add('x'); }, 5);"
            "</script></body></html>", "", 3, directory);
        require(mutated.status == 0, "the :has sibling mutation run exits cleanly");
        int restyled[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 5, restyled), "the restyled row is readable");
        require(is_magenta(restyled), "a sibling gaining a class restyles the row before it");
        std::system(("rm -rf " + directory).c_str());
    }

    // `:has(<relative selector list>)`. Upstream matches it with matches_relative(list, anchor,
        // anchor, context): a combinator plus a selector, descendant unless written. The sibling
        // combinators are not matched anywhere here, so `:has(+ .x)` answers false.
        auto has_page = [](std::string const& rule) {
            return "<html><head><title>Has</title><style>body{margin:0}"
                   ".box{width:400px;height:20px;background:#00ff00}"
                   ".x{width:50px;height:10px;background:#ffff00}"
                 + rule +
                   "</style></head><body>"
                   "<div class='box'><span class='x'>x</span></div>"
                   "<div class='box'><em><span class='x'>x</span></em></div>"
                   "</body></html>";
        };
        struct HasCase {
            char const* what;
            std::string html;
            bool direct_matches;
            bool deep_matches;
        };
        std::vector<HasCase> const has_cases {
            { ":has(.x) matches a descendant at any depth", has_page(".box:has(.x){background:#ff00ff}"), true, true },
            { ":has(> .x) matches a direct child only", has_page(".box:has(> .x){background:#ff00ff}"), true, false },
            { ":has(.nope) matches nothing", has_page(".box:has(.nope){background:#ff00ff}"), false, false },
        };
        for (auto const& has_case : has_cases) {
            auto result = run_window(binary, has_case.html, "", 2, directory);
            std::string const message = std::string("the :has run for ") + has_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int direct[3] = { 0, 0, 0 };
            int deep[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 300, 34 + 10, direct), "the :has pixel is readable");
            require(frame_pixel(directory, 300, 34 + 30, deep), "the second :has pixel is readable");
            if (has_case.direct_matches)
                require(is_magenta(direct), has_case.what);
            else
                require(is_green(direct), has_case.what);
            if (has_case.deep_matches)
                require(is_magenta(deep), has_case.what);
            else
                require(is_green(deep), has_case.what);
        }

        // The invalidation direction: a child arriving restyles the ancestor that has it. The dirty
        // sets already include ancestors of any mutation, so this needed no change - asserted
        // because "it already works" is a claim, not a reason to skip the test.
        auto has_mutation = run_window(binary,
            "<html><head><title>Has mutation</title><style>body{margin:0}"
            "#box{width:400px;height:100px;background:#00ff00}#box:has(.x){background:#ff00ff}"
            ".x{width:50px;height:10px;background:#ffff00}"
            "</style></head><body><div id='box'></div><script>setTimeout(function () {"
            "var s = document.createElement('span'); s.setAttribute('class', 'x');"
            "document.getElementById('box').appendChild(s); }, 5);</script></body></html>",
            "", 2, directory);
        require(has_mutation.status == 0, "the :has mutation run exits cleanly");
        int restyled[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 300, 34 + 80, restyled), "the :has mutation pixel is readable");
        require(is_magenta(restyled), "appending a matching child restyles the :has ancestor");

        // `:not()` takes a compound, and a positional pseudo-class inside it still works.
        auto negated = run_window(binary,
            "<html><head><title>Not</title><style>body{margin:0}"
            "div{width:400px;height:10px;background:#00ff00}div:not(:first-child){background:#0000ff}"
            "</style></head><body><div>a</div><div>b</div><div>c</div></body></html>", "", 2, directory);
        require(negated.status == 0, "the :not run exits cleanly");
        int first[3] = { 0, 0, 0 };
        int second[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 5, first), "the :not first pixel is readable");
        require(frame_pixel(directory, 200, 34 + 15, second), "the :not second pixel is readable");
        require(is_green(first), "div:not(:first-child) does not match the first div");
        require(is_blue(second), "div:not(:first-child) matches the second");
        std::system(("rm -rf " + directory).c_str());
    }

    // A structural change invalidates the child sequence it changed, not the document. Upstream
    // records a tree delta for the departed or arrived sequence (StyleEngineInput.cpp); this is
    // the same set, and when the sheet has no positional selector it is the parent alone.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r' id='r" + std::to_string(index) + "'>row " + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Structural cost</title><style>body{margin:0}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "#r150{background:#0000ff}"
                                 "span{background:#ff00ff}";
        std::string const append = "<script>setTimeout(function () { var d = document.createElement('span');"
                                   "d.textContent = 'x'; document.getElementById('r150').appendChild(d); }, 5);"
                                   "</script></body></html>";

        // Appending inside one row of 200: the parent's subtree is what can see it.
        auto deep = run_window(binary, page + "</style></head><body>" + rows + append, "", 2);
        require(deep.status == 0, "the deep append run exits cleanly");
        int deep_resolves = summary_count(deep.output, ", resolves: ");
        require(deep_resolves > 0 && deep_resolves < 40,
            "appending inside one row of 200 re-resolves a handful of nodes, not the page");

        // Appending to body with no positional selector in the sheet: only the parent can restyle,
        // and the new child has no cache entry to reuse.
        std::string const append_to_body = "<script>setTimeout(function () { var d = document.createElement('div');"
                                           "d.textContent = 'new'; document.body.appendChild(d); }, 5);"
                                           "</script></body></html>";
        auto body_append = run_window(binary, page + "</style></head><body>" + rows + append_to_body, "", 2);
        require(body_append.status == 0, "the body append run exits cleanly");
        int body_resolves = summary_count(body_append.output, ", resolves: ");
        require(body_resolves > 0 && body_resolves < 40,
            "appending to body re-resolves the parent and the new node, not the 200 rows");

        // The control: one positional selector in the sheet and the same append must go coarse,
        // because :first-child can restyle any row when the sequence changes.
        auto positional = run_window(binary,
            page + "div.r:first-child{background:#ffff00}</style></head><body>" + rows + append_to_body, "", 2);
        require(positional.status == 0, "the positional run exits cleanly");
        require(summary_count(positional.output, ", resolves: ") > 200,
            "a positional selector makes a structural change re-resolve the child sequence");

        // And the same for the -of-type family, which is the entry most likely to be forgotten when
        // a pseudo-class is added: uses_positional_pseudo_class() is one list for both.
        auto of_type = run_window(binary,
            page + "div.r:first-of-type{background:#ffff00}</style></head><body>" + rows + append_to_body, "", 2);
        require(of_type.status == 0, "the of-type run exits cleanly");
        require(summary_count(of_type.output, ", resolves: ") > 200,
            ":first-of-type also makes a structural change re-resolve the child sequence");

        // `:empty` is the parent's own state, so the parent is dirtied even when no sibling can
        // see the change. Three runs, because one direction cannot fail: an empty box must be blue
        // (the :empty rule), and appending or removing a child must turn it magenta and back.
        //
        // The first version of this assertion only checked the append and expected magenta - which
        // passed for two items' worth of commits while `:empty` was not implemented at all, because
        // the base colour was magenta too. A control that cannot come out the other way is not one.
        std::string const directory = "aetheris-structural-frames";
        std::string const empty_page = "<html><head><title>Empty</title><style>body{margin:0}"
                                        "#box{width:400px;height:100px;background:#ff00ff}"
                                        "#box:empty{background:#0000ff}</style></head><body>";
        struct EmptyCase {
            char const* what;
            std::string html;
            bool expect_blue;
        };
        std::vector<EmptyCase> const cases {
            { "an empty box matches :empty",
                empty_page + "<div id='box'></div></body></html>", true },
            { "appending a child stops :empty matching",
                empty_page + "<div id='box'></div><script>setTimeout(function () {"
                       "var d = document.createElement('span'); d.textContent = 'x';"
                       "document.getElementById('box').appendChild(d); }, 5);</script></body></html>",
                false },
            { "removing the only child matches :empty again",
                empty_page + "<div id='box'><span id='sp'>s</span></div><script>setTimeout(function () {"
                       "document.getElementById('sp').remove(); }, 5);</script></body></html>",
                true },
        };
        for (auto const& empty_case : cases) {
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto result = run_window(binary, empty_case.html, "", 2, directory);
            std::string const message = std::string("the :empty run for ") + empty_case.what + " exits cleanly";
            require(result.status == 0, message.c_str());
            int box_pixel[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + 50, box_pixel), "the :empty pixel is readable");
            if (empty_case.expect_blue)
                require(is_blue(box_pixel), empty_case.what);
            else
                require(is_magenta(box_pixel), empty_case.what);
        }
        std::system(("rm -rf " + directory).c_str());
    }

    // A click is not a layout. dispatch_click_at() used to call relayout() unconditionally, and
    // relayout() with no mutation on record is a structural change to it: the style cache was
    // cleared and the whole page laid out. On a 405-node page that was 407 resolves and 405 nodes
    // laid out, for a handler that only logged.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'>row " + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Click cost</title><style>body{margin:0}"
                                 "#a{width:400px;height:100px;background:#0000ff}"
                                 "#a.grow{width:200px}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "</style></head><body><div id='a'>A</div>" + rows + "<script>";

        auto static_page = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "console.log('MARK clicked'); });</script></body></html>",
            "--click 200 80", 3);
        require(static_page.status == 0, "the static click run exits cleanly");
        require(static_page.output.find("MARK clicked") != std::string::npos,
            "the handler still runs when the click changes nothing");
        require(summary_count(static_page.output, ", layouts: ") == 1,
            "a click that changes nothing does not run the layout engine");

        // A click that writes an attribute no rule mentions rebuilds and then skips: the pass is
        // entered, and it lays nothing out.
        auto attribute = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "document.getElementById('a').setAttribute('class', 'x'); });</script></body></html>",
            "--click 200 80", 3);
        require(attribute.status == 0, "the attribute click run exits cleanly");
        require(summary_count(attribute.output, ", layouts: ") == 1,
            "a click writing an unobserved attribute does not run the layout engine");
        require(summary_count(attribute.output, ", layout nodes: ") == 0,
            "and the counter says nothing was laid out, rather than repeating the last pass");

        // The control: a click that changes a width must reflow, and only the chain.
        auto layout_change = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "document.getElementById('a').setAttribute('class', 'grow'); });</script></body></html>",
            "--click 200 80", 3);
        require(layout_change.status == 0, "the layout-click run exits cleanly");
        require(summary_count(layout_change.output, ", layouts: ") == 2,
            "a click that changes a width does run the layout engine");
        int clicked_nodes = summary_count(layout_change.output, ", layout nodes: ");
        require(clicked_nodes > 0 && clicked_nodes < 40,
            "and lays out the chain, not the page");
    }

    // The CSSStyleDeclaration surface upstream's IDL declares - cssText, getPropertyValue,
    // setProperty, removeProperty, length, item - read back by the page rather than inferred.
    // `el.style.background = v` is *not* in that IDL (it has no named-property accessor), so it is
    // not tested and not claimed.
    {
        auto result = run_window(binary,
            "<html><head><title>CSSOM</title><style>body{margin:0}"
            ".box{width:400px;height:100px;background:#0000ff}</style></head>"
            "<body><div id='a' class='box'>A</div><script>"
            "var a = document.getElementById('a');"
            "a.style.setProperty('background', '#ff00ff');"
            "console.log('MARK cssText ' + a.style.cssText);"
            "console.log('MARK get ' + a.style.getPropertyValue('background'));"
            "console.log('MARK length ' + a.style.length);"
            "console.log('MARK item ' + a.style.item(0));"
            "console.log('MARK removed ' + a.style.removeProperty('background'));"
            "console.log('MARK after ' + a.style.length);"
            "</script></body></html>", "", 2);
        require(result.status == 0, "the CSSOM run exits cleanly");
        require(result.output.find("MARK cssText background: #ff00ff") != std::string::npos,
            "cssText reads the declarations back");
        require(result.output.find("MARK get #ff00ff") != std::string::npos,
            "getPropertyValue returns the declared value");
        require(result.output.find("MARK length 1") != std::string::npos,
            "length counts the declarations");
        require(result.output.find("MARK item background") != std::string::npos,
            "item(0) is the first property name");
        require(result.output.find("MARK removed #ff00ff") != std::string::npos,
            "removeProperty returns what it removed");
        require(result.output.find("MARK after 0") != std::string::npos,
            "and the declaration is gone");
    }

    // Reftests, in upstream's shape: Tests/LibWeb/Ref pairs an input with a reference document
    // (`<link rel="match">`) and requires them to render identically. Here the input is a page
    // mutated at runtime into a state and the reference is that same state loaded fresh - the only
    // reference that cannot inherit my assumptions about what the incremental path produces.
    //
    // Every case runs over a 200-row page so that reuse is doing real work, and every case must
    // agree byte for byte below the chrome.
    {
        std::string const style = "<html><head><title>Reftest</title><style>body{margin:0}"
                                  "div.r{width:400px;height:3px;background:#00ff00}"
                                  "#mid{background:#0000ff}"
                                  "#mid.hit{width:200px}"
                                  "#mid.hot{background:#ff00ff}"
                                  "span.s{background:#ffff00}"
                                  "</style></head><body>";
        std::string const rows = [] {
            std::string out;
            for (int index = 0; index < 200; ++index) {
                if (index == 100)
                    out += "<div class='r' id='mid'>row 100</div>";
                else
                    out += "<div class='r'>row " + std::to_string(index) + "</div>";
            }
            return out;
        }();
        std::string const tail = "</body></html>";

        std::string const script_open = "<script>";
        std::string const script_close = "</script>";
        auto script_of = [&](std::string const& body) { return script_open + body + script_close; };

        struct Run {
            char const* what;
            std::string mutated;
            std::string baked;
        };
        std::vector<Run> const runs {
            { "a text write",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').textContent = 'ROW 100'; }, 5);") + tail,
                style + rows + tail },
            { "a width change by class",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').setAttribute('class', 'r hit'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hit' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "a paint-only class change",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').setAttribute('class', 'r hot'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            // setAttribute, not className: this engine has classList but no className setter, so
            // `s.className = 's'` would set a JS property and never reach the DOM - which is how
            // this case first failed, on the fixture rather than the engine.
            { "an appended child",
                style + rows + script_of("setTimeout(function () { var s = document.createElement('span'); s.setAttribute('class', 's'); s.textContent = 's'; document.getElementById('mid').appendChild(s); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r' id='mid'>row 100<span class='s'>s</span></div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "a classList.add",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').classList.add('hot'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "a className write",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').className = 'r hot'; }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "an el.style cssText write",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').style = 'background:#ff00ff'; }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "an el.style.setProperty write",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').style.setProperty('background', '#ff00ff'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "an innerHTML replace",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').innerHTML = \"<span class='s'>s</span>\"; }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r' id='mid'><span class='s'>s</span></div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
        };

        std::string const mutated_directory = "aetheris-reftest-mutated";
        std::string const reference_directory = "aetheris-reftest-reference";
        for (auto const& run : runs) {
            std::system(("rm -rf " + mutated_directory + " && mkdir -p " + mutated_directory).c_str());
            std::system(("rm -rf " + reference_directory + " && mkdir -p " + reference_directory).c_str());
            auto mutated = run_window(binary, run.mutated, "", 3, mutated_directory);
            std::string const input_message = std::string("the reftest input for ") + run.what + " exits cleanly";
            if (mutated.status != 0) {
                std::fprintf(stderr, "FAILED: %s (status %d)\n%s\n", input_message.c_str(), mutated.status,
                    mutated.output.substr(0, 400).c_str());
                std::exit(1);
            }
            auto reference = run_window(binary, run.baked, "", 2, reference_directory);
            std::string const reference_message = std::string("the reftest reference for ") + run.what + " exits cleanly";
            require(reference.status == 0, reference_message.c_str());
            require(frames_match_below_chrome(mutated_directory, reference_directory, 34), run.what);
        }
        std::system(("rm -rf " + mutated_directory + " && rm -rf " + reference_directory).c_str());
    }

    // The style cache survives an interaction-state change: only the elements whose
    // pseudo-class state moved are re-resolved, and a state that changes nothing but paint
    // does not run the layout engine. Upstream's ancestor_traversal_for_pseudo_class() is
    // the shape - `:focus-within` is the one pseudo-class that reaches ancestors.
    {
        // A hover that changes only a background: one relayout, still one layout.
        std::string directory = "aetheris-state-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto paint = run_window(binary,
            "<html><head><title>State paint</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:hover{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3, directory);
        require(paint.status == 0, "the hover-paint run exits cleanly");
        require(summary_count(paint.output, ", relayouts: ") == 1, "a hover relayouts once");
        require(summary_count(paint.output, ", layouts: ") == 1,
            "a hover that only changes paint does not run the layout engine");
        int hovered[3] = { 0, 0, 0 };
        int below[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, hovered), "the hovered pixel is readable");
        require(frame_pixel(directory, 200, 34 + 150, below), "the control pixel is readable");
        require(is_magenta(hovered), "the hover restyled the element it moved onto");
        require(!is_magenta(below), "and nothing outside it");

        // The control: the same hover changing width must run the engine.
        auto width = run_window(binary,
            "<html><head><title>State width</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:hover{width:200px}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3);
        require(width.status == 0, "the hover-width run exits cleanly");
        require(summary_count(width.output, ", relayouts: ") == 1, "a width hover relayouts once");
        require(summary_count(width.output, ", layouts: ") == 2,
            "a hover that changes width does run the layout engine");

        // The cache is what makes it cheap: a hover on one row of a 200-row page must
        // re-resolve a handful of nodes, not the page. Before this it cleared the whole
        // cache and resolved all 406.
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'>row " + std::to_string(index) + "</div>";
        auto many = run_window(binary,
            "<html><head><title>State many</title><style>body{margin:0}"
            "#target{width:400px;height:20px;background:#0000ff}"
            "div.r{width:400px;height:3px;background:#00ff00}"
            "#target:hover{background:#ff00ff}</style></head>"
            "<body><div id='target'>t</div>" + rows + "</body></html>",
            "--move 200 44", 3);
        require(many.status == 0, "the 200-row hover run exits cleanly");
        require(summary_count(many.output, ", layouts: ") == 1,
            "a hover in a 200-row page still costs no layout");
        int resolved = summary_count(many.output, ", resolves: ");
        require(resolved > 0 && resolved < 40,
            "a hover in a 200-row page re-resolves a handful of nodes, not the page");
        // The page is 406 nodes; the key comparison must be over the 5 that restyled, since
        // only a node whose style was re-resolved can have a different key.
        require(summary_count(many.output, ", key checks: ") == resolved,
            "the layout key comparison covers the nodes that restyled, not the page");
        require(many.output.find("relayouts: 0") == std::string::npos,
            "and it still relayouts, so the hover is not dropped");

        // A descendant selector on a hovered ancestor: the subtree is dirty too.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto descendant = run_window(binary,
            "<html><head><title>State descendant</title><style>body{margin:0}"
            "#card{width:400px;height:100px;background:#0000ff}"
            "#card .title{width:100px;height:40px;background:#00ff00}"
            "#card:hover .title{background:#ff00ff}</style></head>"
            "<body><div id='card'><span class='title'>t</span></div></body></html>",
            "--move 200 80", 3, directory);
        require(descendant.status == 0, "the hover-descendant run exits cleanly");
        require(summary_count(descendant.output, ", layouts: ") == 1,
            "a descendant selector on a hovered ancestor costs no layout");
        int title_pixel[3] = { 0, 0, 0 };
        int card_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 10, title_pixel), "the descendant pixel is readable");
        require(frame_pixel(directory, 200, 34 + 80, card_pixel), "the card pixel is readable");
        require(is_magenta(title_pixel), "a descendant of the hovered element restyled");
        require(is_blue(card_pixel), "and the hovered element kept its own colour");

        // `:focus-within` is the one pseudo-class that propagates to ancestors, so a focus
        // move has to restyle the chain above - and a descendant of that ancestor that is a
        // sibling of the focused element, which is the case a chain-only dirty set misses.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto within = run_window(binary,
            "<html><head><title>State focus-within</title><style>body{margin:0}"
            "#card{width:400px;height:200px;background:#0000ff}"
            "#card .title{width:400px;height:40px;background:#00ff00}"
            "#card:focus-within{background:#ffff00}"
            "#card:focus-within .title{background:#ff00ff}"
            "input{width:200px;height:40px}</style></head>"
            "<body><div id='card'><span class='title'>t</span><input id='field'></div></body></html>",
            "--focus-key 200 140", 3, directory);
        require(within.status == 0, "the focus-within run exits cleanly");
        require(summary_count(within.output, ", relayouts: ") == 1, "a focus change relayouts once");
        require(summary_count(within.output, ", layouts: ") == 1,
            "a focus change that only changes paint does not run the layout engine");
        int ancestor_pixel[3] = { 0, 0, 0 };
        int sibling_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 150, ancestor_pixel), "the ancestor pixel is readable");
        require(frame_pixel(directory, 200, 34 + 10, sibling_pixel), "the sibling pixel is readable");
        require(ancestor_pixel[0] > 200 && ancestor_pixel[1] > 200 && ancestor_pixel[2] < 100,
            ":focus-within restyled the ancestor, so the ancestor chain is in the dirty set");
        require(is_magenta(sibling_pixel),
            "a descendant of the :focus-within ancestor restyled, so its subtree is too");

        // An ancestor's subtree is only dirtied when a rule can put `:focus-within` on *that*
        // element. Asked sheet-wide, one `.card:focus-within .title` rule made every focus move
        // in the document coarse; asked per element, it only affects the card.
        //
        // The title's base rule is `.title`, one class, so the `:focus-within` rule below wins
        // on specificity. With `#card .title` as the base it would win instead, the rule would
        // never apply, and this would have been a fixture that tested nothing.
        {
            std::string rows;
            for (int index = 0; index < 200; ++index)
                rows += "<div class='r'>row " + std::to_string(index) + "</div>";
            std::string const page = "<html><head><title>State focus rows</title><style>body{margin:0}"
                                     "#card{width:400px;height:100px;background:#0000ff}"
                                     ".title{width:100px;height:20px;background:#00ff00}"
                                     "div.r{width:400px;height:3px;background:#00ff00}"
                                     "input{display:block;width:100px;height:30px;margin:0}";
            std::string const body = "</style></head><body><div id='card'><span class='title'>t</span>"
                                     "<input id='inside'></div><input id='outside'>" + rows
                + "</body></html>";
            std::string const directory = "aetheris-focus-frames";

            // A rule of that shape exists, but the focus is outside the card, so the card's
            // subtree is not dirty - the sheet-wide answer would have dirtied the page.
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto outside = run_window(binary,
                page + "#card:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 149", 3, directory);
            require(outside.status == 0, "the focus-outside run exits cleanly");
            require(summary_count(outside.output, ", layouts: ") == 1,
                "a focus move on a 200-row page costs no layout");
            int outside_resolves = summary_count(outside.output, ", resolves: ");
            require(outside_resolves > 0 && outside_resolves < 40,
                "a rule on another card's :focus-within does not make this focus move coarse");
            int untouched[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + 10, untouched), "the title pixel is readable");
            require(is_green(untouched), "a card that is not focus-within keeps its title");

            // The same rule, focus inside the card: cheap, and the title must restyle.
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto inside = run_window(binary,
                page + "#card:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 74", 3, directory);
            require(inside.status == 0, "the focus-inside run exits cleanly");
            int inside_resolves = summary_count(inside.output, ", resolves: ");
            require(inside_resolves > 0 && inside_resolves < 40,
                "the card the focus moved into is re-resolved, and the page is not");
            int restyled[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + 10, restyled), "the restyled title pixel is readable");
            require(is_magenta(restyled), "the descendant of the :focus-within card restyled");

            // The control: a compound that matches a wide ancestor must go coarse, or the two
            // assertions above would hold whether or not the sheet was ever consulted.
            auto coarse = run_window(binary,
                page + "body:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 74", 3);
            require(coarse.status == 0, "the coarse focus run exits cleanly");
            int coarse_resolves = summary_count(coarse.output, ", resolves: ");
            require(coarse_resolves > 200,
                "a :focus-within rule on body re-resolves the page");
            std::system(("rm -rf " + directory).c_str());
        }

        // `:active` is a separate branch of the same switch and must take the same path.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto pressed = run_window(binary,
            "<html><head><title>State active</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:active{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--press 200 80", 3, directory);
        require(pressed.status == 0, "the :active run exits cleanly");
        require(summary_count(pressed.output, ", relayouts: ") == 1, ":active relayouts once");
        require(summary_count(pressed.output, ", layouts: ") == 1,
            "an :active that only changes paint does not run the layout engine");
        int pressed_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, pressed_pixel), "the :active pixel is readable");
        require(is_magenta(pressed_pixel), ":active restyled the pressed element");

        // The control: `:focus-within` changing width must run the engine.
        auto within_width = run_window(binary,
            "<html><head><title>State focus-within width</title><style>body{margin:0}"
            "#card{width:400px;height:200px;background:#0000ff}"
            "#card:focus-within{width:200px}"
            "input{width:200px;height:40px}</style></head>"
            "<body><div id='card'><input id='field'></div></body></html>",
            "--focus-key 200 100", 3);
        require(within_width.status == 0, "the focus-within width run exits cleanly");
        require(summary_count(within_width.output, ", layouts: ") == 2,
            "a focus change that changes width does run the layout engine");
        std::system(("rm -rf " + directory).c_str());
    }

    // The frame dumps above total ~58 MB and this sandbox's /tmp is a 64 MB tmpfs, so a
    // second run in the same sandbox used to start with /tmp full and fail elsewhere for
    // a reason that had nothing to do with the engine - AetherisDomHost's "render exits 0"
    // was a full disk, not a bad render. Clear them on the way out.
    std::system("rm -rf /tmp/aetheris-*-frames");


    cleanup_frames();
    return 0;
}
