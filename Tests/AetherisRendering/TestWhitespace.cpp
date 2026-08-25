#include "text_layout.hpp"
#include <cassert>
using namespace aetheris::rendering; int main(){assert(TextLayout::collapse_whitespace("a  b\n c")=="a b c");assert(TextLayout::collapse_whitespace("a\nb",true)=="a\nb");}
