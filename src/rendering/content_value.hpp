#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// The `content` property as a list of items rather than a string, which is what it means: `content: "a" "b"`
// is two items and renders "ab", and the part after `/` is alternative text for a screen reader rather than
// text on the page.
//
// Upstream models it the same way - `ContentStyleValue` holds a `StyleValueList` plus an optional alt-text
// list (`Libraries/LibWeb/CSS/StyleValues/ContentStyleValue.h:16-45`), and the tree builder asks for one of
// three kinds, Normal, None or List (`Rust/src/layout/tree_builder.rs:1714`, `FfiComputedContentType`).
// This is that shape at this engine's scale.
struct ContentValue {
    enum class Kind {
        Normal, // `content: normal` - no box
        None, // `content: none` - no box
        List, // one or more items, which generate a box even when every item is empty
    };

    Kind kind { Kind::Normal };
    // The rendered items in order. Only string items are supported; `attr()`, `counter()` and the
    // quote keywords are not, and `unsupported_items` counts what was dropped so a caller can tell an
    // empty declaration from one this engine cannot express.
    std::vector<std::string> items;
    std::vector<std::string> alt_text;
    std::size_t unsupported_items { 0 };

    // The text a generated box renders: the items, concatenated, with no separator of their own.
    std::string text() const;
    // The alternative text, concatenated the same way. Empty when the declaration had no `/`.
    std::string alt() const;
    bool generates_box() const { return kind == Kind::List; }
};

// Parses the declaration value as the CSS parser hands it over: string items wrapped in single quotes, the
// alt-text part after a top-level `/`. See css_parser.cpp for where those quotes are written.
ContentValue parse_content_value(std::string_view raw);

} // namespace aetheris::rendering
