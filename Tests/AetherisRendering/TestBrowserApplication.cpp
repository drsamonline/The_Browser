#include "browser_application.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){BrowserApplication app(800,600);assert(app.tab_count()==1);auto a=*app.active_tab_index();auto b=app.new_tab();assert(app.tab_count()==2&&b!=a);assert(app.switch_to_tab(a));app.resize(1024,768);assert(app.active_session()->viewport().width()==1024);assert(app.close_tab(b));assert(app.tab_count()==1);assert(app.pointer_release(0,0,BrowserApplication::PointerButton::Primary)==false);return 0;}
