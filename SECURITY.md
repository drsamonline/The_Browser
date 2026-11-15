# Security Policy

**Version:** 2.0 · **Effective:** 15 November 2026 · **Owner:** Dr Sohil Momin — sohilmomin2000@gmail.com

This policy covers **Aetheris**, the browser engine in this repository. It does
not cover the upstream Ladybird/SerenityOS code that this project derives from;
report upstream issues to the upstream project.

## Reporting a vulnerability

- **Preferred:** open a private security advisory on this repository
  (Security → Advisories → New draft advisory).
- **Alternative:** email sohilmomin2000@gmail.com with `AETHERIS SECURITY` in the
  subject line.

Please include: affected version or commit, platform, the steps to reproduce, and
what you expected instead. A minimal HTML file that triggers the problem is worth
more than a description.

**Do not** open a public issue for a memory-safety bug, a sandbox escape, a
certificate-validation bypass, or a policy bypass (CSP, mixed content,
same-origin).

## What is in scope

| Area | Examples |
| --- | --- |
| Memory safety | Out-of-bounds reads in the parser, rasteriser, image decoders |
| Network | TLS verification bypass, cookie scoping errors, request smuggling |
| Security policy | CSP bypass, mixed-content bypass, `SecureResourceLoader` bypass |
| Denial of service | A page that hangs or exhausts memory during parse, layout or paint |

## What is out of scope

- Missing features that are documented as not implemented (see
  [docs/ROADMAP.md](docs/ROADMAP.md)) — there is no process isolation, no
  same-origin enforcement in script bindings, and no shaping.
- Anything requiring a malicious local font, a modified binary, or an attacker
  who already runs code on the machine.
- Crashes in the upstream Ladybird tree, which this project does not build.

## Response

This is a single-maintainer project with no guaranteed response time. A report
that includes a reproduction gets a fix or an explanation; a report without one
may sit. Fixes land with a regression test where the bug is testable.

## Supported versions

Only the latest tagged release receives fixes. Older tags are kept for
provenance and are not patched.

## Disclosure

Once a fix is released, the advisory is published with credit unless you ask to
remain anonymous.
