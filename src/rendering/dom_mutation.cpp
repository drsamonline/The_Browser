#include "dom_mutation.hpp"

#include "dom.hpp"

namespace aetheris::rendering {

namespace {
// Deliberately a plain counter, not an atomic: every mutation in this engine happens
// on the thread that owns the document, and a relaxed increment would hide a real
// threading bug rather than surface it.
std::size_t g_serial { 0 };
std::string g_last_attribute;
DomNode const* g_last_node { nullptr };
} // namespace

void note_dom_mutation()
{
    g_last_node = nullptr;
    g_last_attribute.clear();
    ++g_serial;
}

void note_dom_mutation_at(DomNode const* node)
{
    g_last_node = node;
    g_last_attribute.clear();
    ++g_serial;
}

void note_text_mutation(DomNode const* node)
{
    // Same state as note_dom_mutation_at(): the node, and no attribute name. The
    // separate entry point is the one the text path calls, so the distinction a host
    // reads (node set, name empty) is visible at the call site and not only here.
    note_dom_mutation_at(node);
}

DomNode const* last_mutated_node()
{
    return g_last_node;
}

void note_attribute_mutation(std::string const& name)
{
    g_last_attribute = name;
    ++g_serial;
}

std::string const& last_attribute_mutation()
{
    return g_last_attribute;
}

std::size_t dom_mutation_serial()
{
    return g_serial;
}

} // namespace aetheris::rendering
