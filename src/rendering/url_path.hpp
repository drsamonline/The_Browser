#pragma once

#include <string>
#include <vector>

namespace aetheris::rendering {

// Removes "." and ".." segments from a URL path.
//
// Ported from upstream's parser, which works on the serialisation buffer as it goes
// (Libraries/LibURL/Rust/src/url/parser.rs, the Path state):
//
//   on "..": truncate to the segment start, shorten the path (pop one segment), and push "/"
//            unless the current character is already a slash - so "/usr/.." is "/", not ""
//   on ".":  truncate to the segment start plus one
//   else:    append the segment
//
// This lives in its own header, not in url.hpp, because url.hpp already owns the `Url` type.
//
// NOT WIRED INTO NAVIGATION. A first version of this shipped in resolve_url() on 2026-11-18 and
// hung the browser on a relative "../up" navigation; the hang is not explained yet, so the function
// is tested on its own until it is. See docs/CHECKLIST.md row 1g6.
inline std::string remove_dot_segments(std::string const& path)
{
    std::vector<std::string> segments;
    std::string current;
    bool trailing_slash = false;
    auto push_segment = [&] {
        if (current.empty()) {
            // A doubled slash: the spec keeps one separator, not an empty segment.
            trailing_slash = true;
        } else if (current == ".") {
            trailing_slash = true;
        } else if (current == "..") {
            if (!segments.empty())
                segments.pop_back();
            trailing_slash = true;
        } else {
            segments.push_back(current);
            trailing_slash = false;
        }
        current.clear();
    };
    for (char const c : path) {
        if (c == '/')
            push_segment();
        else
            current += c;
    }
    push_segment();

    std::string out;
    for (auto const& segment : segments)
        out += "/" + segment;
    if (out.empty() || trailing_slash || (!path.empty() && path.back() == '/'))
        out += "/";
    return out.empty() ? "/" : out;
}

} // namespace aetheris::rendering
