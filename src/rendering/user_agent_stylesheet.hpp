#pragma once

#include <string_view>

namespace aetheris::rendering {

// The default styles every page starts from, following the CSS2.1 Appendix D
// sample stylesheet. Without this an <h1> renders as ordinary body text and
// paragraphs have no spacing, which is what a page looks like when no author
// stylesheet applies.
std::string_view user_agent_stylesheet();

} // namespace aetheris::rendering
