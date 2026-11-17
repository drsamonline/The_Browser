#include "user_agent_stylesheet.hpp"

namespace aetheris::rendering {

std::string_view user_agent_stylesheet()
{
    return R"CSS(
html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, li, dl, dt, dd,
blockquote, pre, form, fieldset, table, hr {
    display: block;
}
head, title, meta, link, style, script, base, noscript {
    display: none;
}

body {
    margin: 8px;
    line-height: 1.2;
}

h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold; }
h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold; }
h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold; }
h4 { font-size: 1em; margin: 1.33em 0; font-weight: bold; }
h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold; }
h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold; }

p { margin: 1em 0; }

ul, ol { margin: 1em 0; padding-left: 40px; }
li { margin: 0.2em 0; }
dl { margin: 1em 0; }
dt { font-weight: bold; }
dd { margin-left: 40px; }

blockquote { margin: 1em 40px; }

pre {
    margin: 1em 0;
    font-family: monospace;
    white-space: pre;
}

b, strong { font-weight: bold; }
i, em { font-style: italic; }
u { text-decoration: underline; }
s, strike { text-decoration: line-through; }
small { font-size: 0.83em; }
sub { font-size: 0.83em; }
sup { font-size: 0.83em; }

a { color: #0000ee; text-decoration: underline; }

code, kbd, samp, tt { font-family: monospace; }

hr {
    margin: 0.5em auto;
    border-top: 1px solid #999999;
    height: 0;
}

img { display: inline; }

table { border-collapse: separate; }
th { font-weight: bold; text-align: center; }
td, th { padding: 1px; }

fieldset { margin: 0 2px; padding: 0.35em 0.75em 0.625em; border: 1px solid #999999; }
legend { padding: 0 2px; }

/* Form controls, following LibWeb's Default.css: a text field is a bordered box with
 * a Field background, and a button is a bordered box with a ButtonFace background.
 * The system colours and `inset` borders upstream uses are not supported here, so
 * these are the nearest fixed values; the shape is upstream's, the palette is not. */
input, textarea, select, button {
    display: inline-block;
    border: 1px solid #999999;
    padding: 1px 3px;
    background-color: #ffffff;
    color: #000000;
    /* A fixed width, not a minimum: an inline-block with no intrinsic width fills
     * the line here, which makes a click anywhere hit the control and makes hit
     * testing impossible to test. Upstream sizes a field from its size attribute. */
    width: 12em;
    min-height: 1.2em;
}
button, select, input[type=submit], input[type=button], input[type=reset] {
    background-color: #efefef;
}
textarea { white-space: pre-wrap; min-height: 3em; }
)CSS";
}

} // namespace aetheris::rendering
