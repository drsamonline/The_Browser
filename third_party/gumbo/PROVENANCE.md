# gumbo-parser — vendored

- Upstream: https://github.com/google/gumbo-parser
- Licence: Apache-2.0 (see `LICENSE`)
- Taken at commit: see the vendoring commit message.

gumbo is an implementation of the HTML5 parsing algorithm in C99 with no
outside dependencies. Aetheris uses it to build the DOM from HTML, replacing
the hand-written tokenizer + tree builder. The HTML5 algorithm inserts an
implicit `<html>/<head>/<body>` around fragment input; the conversion in
`src/rendering/document.cpp` unwraps those when the document did not author
them, because the rendering tests treat parsed content as authored.
