#include "WindowTestHarness.hpp"

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

    // element, not a text node (Layout/Node.h, `pseudo_element_generator`), and `RenderTree::emit`
    // required a DOM text node - so a `::before` produced fragments that were never emitted.
    //
    // The assertion is pixels, sampled where the measured frame has ink: the generated "MMMM" is
    // magenta at y 55 (the paragraph starts below the 34 px chrome and the glyphs sit near the
    // baseline), and magenta appears at no other y in that row.
    //
    // `::after` is NOT asserted: it is generated and laid out but does not paint, which is row 3a5.
    {
        std::string const directory = "aetheris-generated-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Generated content</title><style>body{margin:0;font-size:24px}"
            "p{width:400px;height:30px;background:#ffffff}"
            "p::before{content:'MMMM';color:#ff00ff}"
            "p::after{content:'YYYY';color:#ffff00}"
            "</style></head><body><p></p></body></html>", "", 2, directory);
        require(result.status == 0, "the generated-content run exits cleanly");

        // Both runs are on one line and in order: `::before` starts at the left edge, `::after`
        // immediately after it. Each is content-sized, which is what makes them share the line -
        // taking the block path put each on a line of its own and looked like `::after` not painting.
        int magenta_first = -1;
        int yellow_first = -1;
        for (int x = 0; x < 400; ++x) {
            int rgb[3] {};
            if (!frame_pixel(directory, x, 55, rgb))
                break;
            if (magenta_first < 0 && is_magenta(rgb))
                magenta_first = x;
            if (yellow_first < 0 && rgb[0] > 200 && rgb[1] > 200 && rgb[2] < 100)
                yellow_first = x;
        }
        std::system(("rm -rf " + directory).c_str());
        require(magenta_first >= 0, "::before paints its text");
        require(yellow_first >= 0, "::after paints its text");
        require(magenta_first < yellow_first, "::before paints before ::after on the same line");
    }

    // `content: ''` generates an empty box, and `content: none` generates nothing. Upstream models
    // content as a list of items (CSS/StyleValues/ContentStyleValue.h) and asks the tree builder for
    // one of three kinds (ComputedContentType::Normal, None, List) - so an empty string is a List
    // holding one empty item, not the absence of a declaration.
    //
    // Two rows, one character of CSS apart: a magenta inline-block box in the first, nothing in the
    // second. The box is 60x20 at document y 16, which is window y 50-70 below the 34 px chrome.
    {
        std::string const directory = "aetheris-empty-content-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Empty content</title><style>body{margin:0;font-size:24px}"
            "p{width:400px;height:40px;background:#ffffff}"
            "p::before{content:'';display:inline-block;width:60px;height:20px;background:#ff00ff}"
            "p.none::before{content:none;display:inline-block;width:60px;height:20px;background:#ff00ff}"
            "</style></head><body><p></p><p class='none'></p></body></html>", "", 2, directory);
        require(result.status == 0, "the empty-content run exits cleanly");

        int generated[3] {};
        require(frame_pixel(directory, 20, 55, generated), "the generated-content frame is readable");
        int absent[3] {};
        require(frame_pixel(directory, 20, 95, absent), "the second row is readable");
        std::system(("rm -rf " + directory).c_str());

        require(is_magenta(generated), "content: '' generates a box");
        require(!is_magenta(absent), "content: none generates nothing");
    }

    // A fragment change on the same document does not re-fetch. Upstream resolves the indicated part
    // and scrolls to it (DOM/Document.cpp, scroll_to_the_fragment); ours re-fetched the page and lost
    // the scroll position, so an anchor link landed back at the top.
    //
    // Two assertions: the page's script runs once (a reload would run it again), and the target is on
    // screen afterwards. The page is 1000 px tall in a 734 px viewport, so scrolling to the seventh
    // 100 px div clamps to 266 - the target is visible at window y 468, not at the top.
    {
        std::string const directory = "aetheris-fragment-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        std::string divs;
        for (int i = 0; i < 10; ++i)
            divs += i == 7 ? "<div id='target' style='background:#ff00ff'></div>" : "<div></div>";
        auto result = run_window(binary,
            "<html><head><title>Fragment</title><style>body{margin:0}div{height:100px;background:#00ff00}"
            "</style></head><body>" + divs
            + "<script>console.log('MARK loaded');"
              "setTimeout(function () { location.hash = 'target'; }, 5);</script></body></html>",
            "", 4, directory);
        require(result.status == 0, "the fragment run exits cleanly");

        int loaded_count = 0;
        for (size_t at = result.output.find("MARK loaded"); at != std::string::npos;
             at = result.output.find("MARK loaded", at + 1))
            ++loaded_count;

        int target_pixel[3] {};
        require(frame_pixel(directory, 10, 468, target_pixel), "the fragment frame is readable");
        std::system(("rm -rf " + directory).c_str());

        require(loaded_count == 1, "the document is not re-fetched for a fragment change");
        require(is_magenta(target_pixel), "the fragment target is scrolled into view");
    }

    // sessionStorage: the same six members over a second store. Upstream keeps a session bottle map
    // per origin and tab, separate from the local one (HTML/Window.cpp, session_storage).
    {
        auto result = run_window(binary,
            "<html><head><title>Session storage</title><style>body{margin:0}</style></head><body><script>"
            "localStorage.setItem('l', 'local');"
            "sessionStorage.setItem('s', 'session');"
            "console.log('MARK both ' + localStorage.getItem('l') + ' ' + sessionStorage.getItem('s'));"
            "console.log('MARK lens ' + localStorage.length + ' ' + sessionStorage.length);"
            "console.log('MARK separate ' + sessionStorage.getItem('l') + ' ' + localStorage.getItem('s'));"
            "</script></body></html>", "", 2);
        require(result.status == 0, "the session storage run exits cleanly");
        require(result.output.find("MARK both local session") != std::string::npos,
            "each store keeps its own value");
        require(result.output.find("MARK lens 1 1") != std::string::npos, "each store has one key");
        require(result.output.find("MARK separate null null") != std::string::npos,
            "the two stores cannot see each other");
    }

    // `location`'s component setters. Upstream copies the URL, replaces one component and navigates
    // (HTML/Location.cpp), and the hash setter returns early when the fragment is unchanged.
    //
    // Each fixture sets the component once and then reads it back on the next load, so the assertion
    // is that the navigation happened *and* carried the value.
    {
        auto setter_case = [&](std::string const& component, char const* first, char const* second) {
            auto result = run_window(binary,
                "<html><head><title>Location setter</title><style>body{margin:0}</style></head><body><script>"
                "console.log('MARK " + component + " before [' + location." + component + " + ']');"
                "if (!location." + component + ") { location." + component
                    + " = '" + (std::string(component) == "search" ? "q=1" : "#frag") + "'; }"
                // `hash` is a same-document navigation now, so it reads back without a reload;
                // `search` still navigates and is asserted on the next load.
                "console.log('MARK " + component + " after [' + location." + component + " + ']');"
                "</script></body></html>", "", 6);
            require(result.status == 0, "the location-setter run exits cleanly");
            require(result.output.find(first) != std::string::npos, first);
            require(result.output.find(second) != std::string::npos, second);
        };
        // `search` navigates, so its value is asserted on the next load. `hash` is a same-document
        // navigation and is covered by the fragment test below: setting it does not reload, so there is
        // no next load to read it on. Reading it back synchronously does not work yet - row 1g7c.
        setter_case("search", "MARK search before []", "MARK search after [?q=1]");
        // `hash` is a same-document navigation: no reload, so it must read back in the statement after
        // the assignment. This assertion was deleted once on the strength of a probe against a binary
        // 73 minutes old - it was right the first time (row 4d).
        setter_case("hash", "MARK hash before []", "MARK hash after [#frag]");
    }

    // `history.go(delta)`. Upstream: back() and forward() are go(-1) and go(+1), and go(0) reloads
    // the current document (HTML/History.cpp, delta_traverse).
    {
        auto result = run_window(binary,
            "<html><head><title>Go</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK a len ' + history.length);"
            "setTimeout(function () { location.href = '/b'; }, 5);"
            "</script></body></html>",
            "", 8, {},
            "<html><head><title>Go B</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK b len ' + history.length);"
            "if (!window.__reloaded) { window.__reloaded = true; setTimeout(function () { history.go(0); }, 5); }"
            "</script></body></html>");
        require(result.status == 0, "the history.go run exits cleanly");
        // go(0) reloads: the same document is fetched and its script runs a second time.
        auto first = result.output.find("MARK b len 2");
        require(first != std::string::npos, "the second document is two entries deep");
        require(result.output.find("MARK b len 2", first + 1) != std::string::npos,
            "history.go(0) reloads the current document");
    }

    // Session history: `history.length` counts entries, and `location.replace()` overwrites the current
    // one instead of adding. Upstream: HTML/History.idl for the interface, and replace() navigates with
    // replacement enabled (HTML/Location.cpp).
    //
    // The discriminating assertion is the length after a replacement: a push would make it 2.
    {
        auto result = run_window(binary,
            "<html><head><title>History</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK a len ' + history.length);"
            "setTimeout(function () { location.replace('/b'); }, 5);"
            "</script></body></html>",
            "", 5, {},
            "<html><head><title>History B</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK b len ' + history.length + ' url ' + location.href.slice(-2));"
            "</script></body></html>");
        require(result.status == 0, "the history run exits cleanly");
        require(result.output.find("MARK a len 1") != std::string::npos, "a first load is one history entry");
        require(result.output.find("MARK b len 1 url /b") != std::string::npos,
            "location.replace() moves the page without adding a history entry");
    }

    // Navigation, and the cross-load localStorage persistence that depends on it. A click on a link
    // navigates; the interpreter survives the navigation, so the store does too. This is the proof row
    // 1e2 was waiting for, and it needs two documents, which is why run_window() grew a second one.
    {
        auto result = run_window(binary,
            "<html><head><title>Nav A</title><style>body{margin:0}"
            "a{display:block;width:400px;height:40px;background:#0000ff}"
            "</style></head><body><a href='/b'>go</a><script>"
            "localStorage.setItem('k', 'clicked');"
            "console.log('MARK A href ' + (location.href.indexOf('127.0.0.1') !== -1));"
            "</script></body></html>",
            "--click 200,54", 5, {}, 
            "<html><head><title>Nav B</title><style>body{margin:0}</style></head><body><script>"
            "console.log('MARK B href ' + location.href.slice(-2));"
            "console.log('MARK B read ' + localStorage.getItem('k'));"
            "</script></body></html>");
        require(result.status == 0, "the navigation run exits cleanly");
        require(result.output.find("MARK A href true") != std::string::npos, "location.href is the page's own URL");
        require(result.output.find("MARK B href /b") != std::string::npos, "a click on a link navigates");
        require(result.output.find("MARK B read clicked") != std::string::npos,
            "localStorage survives the navigation, which is what makes it storage");
    }

    // localStorage, the Storage interface upstream declares in HTML/Storage.idl: length, key(index),
    // getItem, setItem, removeItem, clear. A missing key is null, not undefined.
    //
    // The store lives on the interpreter, which lives as long as the window, so it survives a
    // navigation by construction. That part is NOT proved end to end here: the harness cannot
    // navigate (no location object, and a click does not follow a link), so this asserts the six
    // members and leaves cross-load persistence as a row in docs/CHECKLIST.md.
    {
        auto result = run_window(binary,
            "<html><head><title>Storage</title><style>body{margin:0}</style></head><body><script>"
            "localStorage.setItem('k', 'v1');"
            "localStorage.setItem('n', '2');"
            "console.log('MARK len ' + localStorage.length);"
            "console.log('MARK get ' + localStorage.getItem('k'));"
            "console.log('MARK missing ' + localStorage.getItem('nope'));"
            "console.log('MARK key0 ' + localStorage.key(0));"
            "console.log('MARK key9 ' + localStorage.key(9));"
            "localStorage.removeItem('k');"
            "console.log('MARK removed ' + localStorage.length);"
            "localStorage.clear();"
            "console.log('MARK cleared ' + localStorage.length);"
            "</script></body></html>", "", 2);
        require(result.status == 0, "the storage run exits cleanly");
        require(result.output.find("MARK len 2") != std::string::npos, "length counts the stored keys");
        require(result.output.find("MARK get v1") != std::string::npos, "getItem returns what setItem stored");
        require(result.output.find("MARK missing null") != std::string::npos, "a missing key is null");
        require(result.output.find("MARK key0 k") != std::string::npos, "key(0) returns a stored key");
        require(result.output.find("MARK key9 null") != std::string::npos, "key() past the end is null");
        require(result.output.find("MARK removed 1") != std::string::npos, "removeItem removes one key");
        require(result.output.find("MARK cleared 0") != std::string::npos, "clear empties the store");
    }

    // The focus and blur events, and the ordering bug that hid them. run_timers() runs every timer due
    // in one frame's budget, so focus() then blur() inside that window used to have the first request
    // overwritten before the loop performed it: the state moved for neither and no event fired. The
    // request is a queue now, and both events arrive in order.
    {
        auto result = run_window(binary,
            "<html><head><title>Focus events</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:focus{background:#ff00ff}"
            "</style></head><body><input id='a'><script>"
            "var a = document.getElementById('a');"
            "a.addEventListener('focus', function () { console.log('MARK F'); });"
            "a.addEventListener('blur', function () { console.log('MARK B'); });"
            "setTimeout(function () { a.focus(); }, 5);"
            "setTimeout(function () { a.blur(); }, 12);"
            "</script></body></html>", "", 5);
        require(result.status == 0, "the focus-event run exits cleanly");
        auto const focus_at = result.output.find("MARK F");
        auto const blur_at = result.output.find("MARK B");
        require(focus_at != std::string::npos, "element.focus() fires a focus event");
        require(blur_at != std::string::npos, "element.blur() fires a blur event");
        require(focus_at < blur_at, "the two events arrive in the order the script asked for them");

        // The same script in one timer, which is where the slot was overwritten: focus, blur, focus
        // must end focused, and the events must come in that order.
        auto ordered = run_window(binary,
            "<html><head><title>Focus order</title><style>body{margin:0}"
            "input{display:block;margin:0;padding:0;border:0;width:100px;height:20px;background:#00ff00}"
            "input:focus{background:#ff00ff}"
            "</style></head><body><input id='a'><script>"
            "var a = document.getElementById('a');"
            "a.addEventListener('focus', function () { console.log('MARK F'); });"
            "a.addEventListener('blur', function () { console.log('MARK B'); });"
            "setTimeout(function () { a.focus(); a.blur(); a.focus(); }, 5);"
            "</script></body></html>", "", 5);
        require(ordered.status == 0, "the focus-order run exits cleanly");
        auto const first_f = ordered.output.find("MARK F");
        auto const b_at = ordered.output.find("MARK B");
        auto const second_f = ordered.output.find("MARK F", first_f == std::string::npos ? 0 : first_f + 1);
        require(first_f != std::string::npos && b_at != std::string::npos && second_f != std::string::npos,
            "three focus calls in one timer all arrive");
        require(first_f < b_at && b_at < second_f, "they arrive in the order the script asked for them");
    }

    // getBoundingClientRect(): the page asserts its own numbers, which is the proof style the plan

    cleanup_frames();
    return 0;
}
