#pragma once

#include <cstddef>
#include <string>

namespace aetheris::rendering {

struct DomNode;

// A document-wide counter bumped at every point that mutates the DOM.
//
// Upstream marks individual nodes dirty (`Node::invalidate_style` walks ancestors
// setting a style-dirty bit); this is the coarse version: one counter for the whole
// document, so a host can tell whether anything changed without walking the tree.
// The window used to fingerprint the document each frame instead, which is O(n) per
// frame on any page with a timer.
void note_dom_mutation();

// A mutation of one node (text or attribute). Records the node so the renderer can
// invalidate that subtree instead of everything; a structural mutation calls plain
// `note_dom_mutation()` and clears it, which invalidates everything.
void note_dom_mutation_at(DomNode const* node);

// The node touched by the most recent mutation, null if it was structural or none.
DomNode const* last_mutated_node();

// A mutation that was an attribute write, so a host can ask whether anything could
// observe it before rebuilding layout.
void note_attribute_mutation(std::string const& name);

// The attribute written by the most recent mutation, empty if the last one was not an
// attribute write.
std::string const& last_attribute_mutation();

// Monotonic. Two equal readings mean nothing has been mutated in between.
std::size_t dom_mutation_serial();

} // namespace aetheris::rendering
