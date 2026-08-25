# Upstream Attribution and Provenance

## Purpose

Aetheris Browser is a derivative project containing code and infrastructure associated with the Ladybird and SerenityOS ecosystem. The repository includes recognizable upstream components and libraries such as `AK`, `LibCore`, and related browser infrastructure.

This document records the repository policy for provenance. It is intentionally conservative: it does not claim a precise upstream revision until a file-level provenance audit has established one.

## Required treatment of upstream-derived code

- Preserve existing copyright notices.
- Preserve license texts and source-level license headers.
- Do not present upstream-derived code as having been written entirely by the Aetheris project.
- Do not apply a blanket Aetheris license to third-party or upstream-derived files without confirming compatibility and authority.
- Record substantial Aetheris-specific modifications separately when practical.

## Required audit

Before a release or broad relicensing statement, perform and record:

1. the exact upstream repository or repositories used;
2. the imported revision or snapshot where recoverable;
3. directories or files copied or derived from upstream;
4. Aetheris-specific files and substantial modifications;
5. applicable licenses for each major provenance group;
6. redistribution and attribution obligations.

## Current statement

Until that audit is complete, Aetheris should be described as an experimental derivative project built on a Ladybird/SerenityOS-derived codebase, not as a browser implemented entirely from scratch.

Official upstream projects include Ladybird and SerenityOS. Consult the license and copyright notices present in the source tree for authoritative legal terms applicable to individual components.
