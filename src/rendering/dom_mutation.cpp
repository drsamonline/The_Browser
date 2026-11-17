#include "dom_mutation.hpp"

namespace aetheris::rendering {

namespace {
// Deliberately a plain counter, not an atomic: every mutation in this engine happens
// on the thread that owns the document, and a relaxed increment would hide a real
// threading bug rather than surface it.
std::size_t g_serial { 0 };
} // namespace

void note_dom_mutation()
{
    ++g_serial;
}

std::size_t dom_mutation_serial()
{
    return g_serial;
}

} // namespace aetheris::rendering
