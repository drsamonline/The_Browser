#include <color.hpp>
#include <content_value.hpp>
#include <css_parser.hpp>
#include <document.hpp>
#include <selector.hpp>
#include <style.hpp>
#include <css_tokenizer.hpp>

#include <cstdlib>
#include <iostream>

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
    // A generated box's style: `::before`/`::after` resolve their own declarations, matched by
    // stripping the pseudo-element suffix and matching the rest against the element. Upstream keeps a
    // pseudo-element's computed style beside the element's (DOM/Element.cpp) and generates a box only
    // when `content` is neither normal nor none (Rust/src/layout/tree_builder.rs).
    {
        CssParser pseudo_parser;
        auto pseudo_sheet = pseudo_parser.parse(
            "p { color: #00ff00; }"
            "p::before { content: 'A'; color: #ff00ff; }"
            "p::after { content: 'B'; }"
            "p.card::before { content: 'C'; font-weight: bold; }");
        auto document = Document::parse_html("<p class='card'>x</p>");
        auto* paragraph = document.root().children.empty() ? nullptr : document.root().children.front().get();
        while (paragraph && paragraph->name != "p")
            paragraph = nullptr;
        require(paragraph != nullptr, "the fixture has a paragraph");

        StyleResolver resolver;
        auto element_style = resolver.resolve(*paragraph, pseudo_sheet);
        require(element_style.get("color") && *element_style.get("color") == "#00ff00", "the element keeps its own colour");

        auto before = resolver.resolve_pseudo_element(*paragraph, pseudo_sheet, "before", &element_style);
        // The CSS parser stores a string value without its quotes, so `content: 'C'` reads back as C.
        require(before.get("content") && *before.get("content") == "'C'", "the more specific ::before wins");
        require(before.get("font-weight") && *before.get("font-weight") == "bold", "its other declarations apply");
        // Colour comes from the *less* specific rule, which also sets it: specificity decides conflicts,
        // not which rules apply.
        require(before.get("color") && *before.get("color") == "#ff00ff", "a declaration the other rule does not set still applies");

        auto after = resolver.resolve_pseudo_element(*paragraph, pseudo_sheet, "after", &element_style);
        require(after.get("content") && *after.get("content") == "'B'", "::after resolves separately");

        // ::after has no colour of its own, so it inherits the element's.
        require(after.get("color") && *after.get("color") == "#00ff00", "::after inherits from the element");
        require(!after.get("font-weight"), "::after does not pick up ::before's declarations");
    }

    CssTokenizer tokenizer("div > .card, * { margin: 10px 20px; } /* comment */");
    auto tokens = tokenizer.tokenize();
    bool child = false;
    bool universal = false;
    for (auto const& token : tokens) {
        child |= token.type == CssTokenType::ChildCombinator;
        universal |= token.type == CssTokenType::UniversalSelector;
    }
    require(child, "child combinator token");
    require(universal, "universal selector token");

    auto sheet = CssParser {}.parse("h1, .title { color: red; margin: 12px; }");
    require(sheet.rules.size() == 1, "rule count");
    require(sheet.rules[0].selectors.size() == 2, "selector list");
    require(sheet.rules[0].declarations.size() == 2, "declaration count");
    require(sheet.rules[0].source_order == 0, "source order");

    // Named colours come from the full CSS table (upstream LibGfx data).
    {
        auto purple = Color::parse("rebeccapurple");
        require(purple.has_value(), "rebeccapurple parses");
        require(purple->red == 0x66 && purple->green == 0x33 && purple->blue == 0x99, "rebeccapurple value");
        auto crimson = Color::parse("crimson");
        require(crimson.has_value() && crimson->red == 0xdc, "crimson parses");
        require(!Color::parse("notacolour").has_value(), "unknown colour rejected");
    }


    // Media queries are resolved against the screen viewport (1024x768).
    {
        CssParser parser;
        // `media_sheet`, not `sheet`: an outer sheet is in scope here and C4456 is
        // fatal under /WX.
        auto media_sheet = parser.parse(
            "p { color: black; }"
            "@media screen { p { color: green; } }"
            "@media print { p { color: red; } }"
            "@media (min-width: 600px) { #a { color: blue; } }"
            "@media (max-width: 500px) { #b { color: orange; } }");
        // screen and min-width survive; print and max-width are dropped.
        require(media_sheet.rules.size() == 3, "media queries filter by the viewport");
        bool has_blue = false;
        for (auto const& rule : media_sheet.rules)
            for (auto const& selector : rule.selectors)
                if (selector == "#a")
                    has_blue = true;
        require(has_blue, "min-width rule is kept");
        for (auto const& rule : media_sheet.rules)
            for (auto const& selector : rule.selectors) {
                require(selector != "#b", "max-width rule is dropped");
                require(selector.rfind("@media", 0) != 0, "media blocks do not leak as selectors");
                require(selector.find('}') == std::string::npos, "block braces do not leak into selectors");
            }
    }

// Row 3a6: `content` is a list of items with optional alt text, not a string. The item boundaries are what
    // make `content: "a" "b"` render "ab" rather than "a b", and everything after `/` is alternative text
    // rather than page text (upstream: ContentStyleValue holds a StyleValueList plus an alt-text list).
    {
        auto const list = parse_content_value("'a' 'b' / 'alt'");
        require(list.generates_box(), "two items generate a box");
        require(list.items.size() == 2, "two items are two items");
        require(list.text() == "ab", "and they concatenate with no separator of their own");
        require(list.alt() == "alt", "the part after the slash is the alt text");

        require(parse_content_value("none").kind == ContentValue::Kind::None, "`none` is None");
        require(!parse_content_value("normal").generates_box(), "`normal` generates no box");

        // `content: ''` is a list holding one empty string: a box, with nothing in it.
        auto const empty = parse_content_value("''");
        require(empty.generates_box() && empty.items.size() == 1 && empty.text().empty(),
            "an empty string item still generates a box");

        // A function this engine cannot express is counted, not rendered as its own source text.
        auto const unsupported = parse_content_value("attr(title)");
        require(unsupported.unsupported_items == 1 && unsupported.text().empty(),
            "attr() is unsupported and renders nothing rather than \"attr(title)\"");
    }

// Row 3d1: `@font-face` is a descriptor block, not a selector rule, and it has to survive parsing with its
    // family and its source intact.
    {
        CssParser face_parser;
        auto sheet = face_parser.parse(
            "@font-face { font-family: 'Body'; src: url(https://example.test/body.woff2) format('woff2'); }"
            "@font-face { font-family: Heading; src: url(/heading.ttf); }"
            "@font-face { src: url(no-family.ttf); }"
            "p { color: #123456; }");
        require(sheet.font_faces.size() == 2, "two faces with a family and a source; the third has no family");
        require(sheet.font_faces[0].family == "Body", "a quoted family keeps its name");
        require(sheet.font_faces[0].source == "https://example.test/body.woff2", "the url is taken out of url()");
        require(sheet.font_faces[1].family == "Heading", "an unquoted family is read too");
        require(sheet.font_faces[1].source == "/heading.ttf", "and its source");
        require(sheet.rules.size() == 1, "the rule after the faces is still parsed");
    }

    // Row 3j: a selector is parsed once, when the stylesheet is parsed, and matching walks that
    // form instead of re-reading the text for every node. Upstream's shape: the parsed selector is
    // an object (LibWeb/CSS/Selector.h) and matching evaluates it
    // (LibWeb/Rust/src/css/dom_selector_matching.rs:1262).
    {
        // The chain and its combinators, in subject-to-leftmost order.
        auto const chain = compile_selector("div.a > .b + span");
        require(chain.parts.size() == 3, "three compounds in `div.a > .b + span`");
        require(chain.parts[0].combinator == ' ', "the leftmost part's combinator is unused");
        require(chain.parts[0].compound.tag == "div" && chain.parts[0].compound.classes.size() == 1,
            "the first compound is `div.a`");
        require(chain.parts[1].combinator == '>', "child combinator between the first and second");
        require(chain.parts[2].combinator == '+', "next-sibling combinator before the subject");
        require(chain.parts[2].compound.tag == "span", "the subject is a span");

        // A descendant combinator is whitespace, and a space inside parentheses is not one.
        auto const descendant = compile_selector("a b");
        require(descendant.parts.size() == 2 && descendant.parts[1].combinator == ' ',
            "whitespace is a descendant combinator");
        auto const nested = compile_selector(":is(.a, .b) .c");
        require(nested.parts.size() == 2, "the space inside :is() is not a combinator");
        require(nested.parts[0].compound.pseudo_classes.size() == 1, ":is() is one pseudo-class");

        // Specificity is computed once, with the parse, and is the same number the cascade used
        // to recompute per node.
        require(compile_selector("div").specificity == 1, "one element is 1");
        require(compile_selector(".a.b").specificity == 200, "two classes are 200");
        require(compile_selector("#id .a div").specificity == 10101, "an id, a class and an element");

        // An attribute selector keeps its operator, its value and its case flag.
        auto const attribute = parse_compound("[data-x~='v' i]");
        require(attribute.attributes.size() == 1, "one attribute requirement");
        require(attribute.attributes[0].op == AttributeRequirement::Operator::Includes, "~= is Includes");
        require(attribute.attributes[0].name == "data-x" && attribute.attributes[0].value == "v",
            "the name and value are separated");
        require(attribute.attributes[0].insensitive, "the `i` flag is read");

        // The sheet carries the compiled form for every selector it parsed, and the text beside it.
        CssParser compiled_parser;
        auto const sheet = compiled_parser.parse("div, .a > .b { color: #000000; }");
        require(sheet.rules.size() == 1, "one rule");
        require(sheet.rules[0].selectors.size() == 2, "two selectors");
        require(sheet.rules[0].compiled.size() == 2, "and both are compiled");
        require(sheet.rules[0].compiled[1].parts.size() == 2, "the second selector has two compounds");

        // The class test scans the attribute in place now, so a class that is only part of a word
        // must still not match.
        auto class_document = Document::parse_html("<div class='alpha beta'></div>");
        auto* div = class_document.root().children.empty() ? nullptr : class_document.root().children.front().get();
        require(div != nullptr, "the fixture has a div");
        auto const class_sheet = CssParser {}.parse(".alpha { color: #ff0000; } .alph { color: #00ff00; }");
        StyleResolver class_resolver;
        auto const alpha = class_resolver.resolve(*div, class_sheet, nullptr, nullptr);
        require(alpha.get("color") && *alpha.get("color") == "#ff0000", "a whole-word class matches");
        require(class_sheet.rules.size() == 2, "both class rules parsed");
    }

    // Row 3j: the style fingerprint is cached, because layout asks every node for its parent's.
    // A cache that does not invalidate would make a child inherit a stale parent style, so the
    // invalidation is asserted rather than assumed.
    {
        StyleProperties properties;
        properties.set("color", "#111111");
        auto const before = properties.fingerprint();
        require(!before.empty(), "a fingerprint is produced");
        require(properties.fingerprint() == before, "and it is stable");
        properties.set("color", "#222222");
        require(properties.fingerprint() != before, "changing a property invalidates the cached digest");
        auto const changed = properties.fingerprint();
        properties.set("background-color", "#333333");
        require(properties.fingerprint() != changed, "adding a property invalidates it too");
    }

    return 0;
}
