#include "document.hpp"
#include "document_runtime.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){auto d=Document::parse_html("<div><button>Go</button></div>");auto* div=d.root().first_child(); assert(div); DocumentRuntime r(d); bool clicked=false;r.add_event_listener(div,DomEventType::Click,[&](DomEvent&){clicked=true;});auto* b=r.append_element(div,"button");assert(b);auto e=r.dispatch(DomEventType::Click,b);assert(clicked&&!e.default_prevented);assert(r.set_attribute(b,"id","new"));assert(r.consume_dirty());assert(!r.dirty());assert(r.remove(b));assert(r.consume_dirty());}
