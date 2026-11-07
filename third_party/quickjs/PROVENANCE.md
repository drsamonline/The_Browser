# QuickJS (quickjs-ng) — vendored

- Upstream: https://github.com/quickjs-ng/quickjs
- Licence: MIT (see `LICENSE`)
- Taken at commit: see the vendoring commit message.

Only the embedding core is vendored: `quickjs.c`, `libregexp.c`,
`libunicode.c`, `dtoa.c` and their headers. The CLI (`qjs.c`), the standard
library (`quickjs-libc.c`) and the test262 runner are not included.

QuickJS provides the JavaScript engine for the standalone profile, replacing
the hand-written interpreter in `src/js/`. It builds with a C compiler only —
no Rust, ICU or clang plugin — which is why the upstream LibJS engine cannot be
used in this build.
