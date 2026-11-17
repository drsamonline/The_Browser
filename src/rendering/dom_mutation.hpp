#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

// A child list of `parent` changed: something was appended, removed, inserted or replaced.
// Records the parent, so the renderer can invalidate that parent's subtree - positional
// selectors and `:empty` are about the siblings - instead of the whole document. Upstream
// records a tree delta for the sequence the node left or joined; this is the same set.
//
// Several child-list changes between two relayouts accumulate, which is why this one does not
// clear the list. Every other note does.
void note_child_list_mutation_at(DomNode const* parent);

// The parents whose child lists changed since the last note of another kind.
std::vector<DomNode const*> const& last_child_list_mutations();

// A mutation of one node (text or attribute). Records the node so the renderer can
// invalidate that subtree instead of everything; a structural mutation calls plain
// `note_dom_mutation()` and clears it, which invalidates everything.
//
// Clears the recorded attribute name, because a node-scoped mutation is not by
// itself an attribute write. A caller that IS one calls `note_attribute_mutation()`
// afterwards, never before: the other order leaves the name cleared and the write
// looking like a text edit, which is what broke the attribute gate.
void note_dom_mutation_at(DomNode const* node);

// A write to a node's text content. Records the node and clears the attribute name,
// so a host can tell a text write (which can reflow) from an attribute write (which
// may only repaint).
void note_text_mutation(DomNode const* node);

// The node touched by the most recent mutation, null if it was structural or none.
DomNode const* last_mutated_node();

// A mutation that was an attribute write, so a host can ask whether anything could
// observe it before rebuilding layout. Call it *after* the node-scoped note: that
// one clears the name, this one sets it.
void note_attribute_mutation(std::string const& name);

// The attribute written by the most recent mutation, empty if the last one was not an
// attribute write.
std::string const& last_attribute_mutation();

// The one way to write or remove an attribute on a live document: it writes and notes, in the
// order js_set_attribute established (node-scoped first, because that clears the recorded
// attribute name).
//
// Upstream has the same choke point through the type system - Element::m_attributes is private
// and every write runs Element::set_attribute -> run_attribute_change_steps ->
// invalidate_style_after_attribute_change. Here the map is public, so a caller that writes it
// directly changes the DOM and tells the renderer nothing: classList, el.style.background and
// el.style = '...' all did exactly that, and each was a silent no-op in the renderer.
void set_attribute(DomNode& node, std::string name, std::string value);
void remove_attribute(DomNode& node, std::string const& name);

// Monotonic. Two equal readings mean nothing has been mutated in between.
std::size_t dom_mutation_serial();

} // namespace aetheris::rendering
