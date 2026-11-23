#pragma once

#include "url.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// A document's Content-Security-Policy, as far as this engine enforces it: the fetch directives and the two inline
// questions. Upstream has a class per directive with a pre-request check
// (LibWeb/ContentSecurityPolicy/Directives/ImageSourceDirective.cpp:27-43: get the effective directive, ask whether
// this directive should execute at all, then match the request against the source list). This is that shape with one
// matcher instead of a class per directive, which is the honest size for an engine that fetches four kinds of thing.
//
// Parsed from the `content` attribute of `<meta http-equiv="Content-Security-Policy">`. A header would be the same
// string; this engine has no response headers yet.
class ContentSecurityPolicy {
public:
    static ContentSecurityPolicy parse(std::string_view policy);

    bool empty() const { return m_directives.empty(); }
    std::string const& raw() const { return m_raw; }

    // `img-src`, `style-src`, `script-src`, `connect-src`, `font-src` - with `default-src` standing in when the
    // named directive is absent, which is what the spec's "should fetch directive execute" step amounts to.
    bool allows_fetch(std::string_view directive, Url const& url, Url const& document_url) const;

    // Inline `<script>`/`<style>` need `'unsafe-inline'` (or no directive restricting them at all). A nonce or a
    // hash is not read yet; `AetherisSecurityPolicy` says so rather than implying otherwise.
    bool allows_inline_script() const;
    bool allows_inline_style() const;

    // The source list that would apply to `directive`: its own, else `default-src`, else empty.
    std::string effective_source_list(std::string_view directive) const;

private:
    // Whether one source expression in a list matches `url`. The list is matched left to right and the first
    // match wins, as the spec's §6.7.2.5 does.
    static bool source_matches(std::string const& source, Url const& url, Url const& document_url);

    std::vector<std::pair<std::string, std::string>> m_directives; // name -> source list, in the policy's order
    std::string m_raw;
};

} // namespace aetheris::rendering
