#pragma once

#include <cstddef>

namespace aetheris::rendering {

// A document-wide counter bumped at every point that mutates the DOM.
//
// Upstream marks individual nodes dirty (`Node::invalidate_style` walks ancestors
// setting a style-dirty bit); this is the coarse version: one counter for the whole
// document, so a host can tell whether anything changed without walking the tree.
// The window used to fingerprint the document each frame instead, which is O(n) per
// frame on any page with a timer.
void note_dom_mutation();

// Monotonic. Two equal readings mean nothing has been mutated in between.
std::size_t dom_mutation_serial();

} // namespace aetheris::rendering
