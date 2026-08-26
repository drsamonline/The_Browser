#include "browser_application.hpp"
namespace aetheris::rendering {
BrowserApplication::BrowserApplication(float w,float h):m_width(w),m_height(h){new_tab();}
std::size_t BrowserApplication::new_tab(){Tab tab;tab.session.set_viewport(m_width,m_height);m_tabs.push_back(std::move(tab));m_active=m_tabs.size()-1;return *m_active;}
bool BrowserApplication::close_tab(std::size_t i){if(i>=m_tabs.size())return false;m_tabs.erase(m_tabs.begin()+i);if(m_tabs.empty()){m_active.reset();return true;}if(*m_active>=m_tabs.size())m_active=m_tabs.size()-1;return true;}
bool BrowserApplication::switch_to_tab(std::size_t i){if(i>=m_tabs.size())return false;m_active=i;return true;}
BrowserSession* BrowserApplication::active_session(){return m_active?&m_tabs[*m_active].session:nullptr;} BrowserSession const* BrowserApplication::active_session()const{return m_active?&m_tabs[*m_active].session:nullptr;}
NavigationResult BrowserApplication::navigate(NavigationRequest const&r,ResourceLoader&l){auto*s=active_session();return s?s->navigate(r,l):NavigationResult{NavigationError::InvalidUrl,"No active tab"};}
NavigationResult BrowserApplication::reload(ResourceLoader&l){auto*s=active_session();return s?s->reload(l):NavigationResult{NavigationError::InvalidUrl,"No active tab"};}
NavigationResult BrowserApplication::back(ResourceLoader&l){auto*s=active_session();return s?s->back(l):NavigationResult{NavigationError::InvalidUrl,"No active tab"};}
NavigationResult BrowserApplication::forward(ResourceLoader&l){auto*s=active_session();return s?s->forward(l):NavigationResult{NavigationError::InvalidUrl,"No active tab"};}
bool BrowserApplication::pointer_move(float x,float y){auto*s=active_session();return s&&s->hover_at(x,y);}
NavigationResult BrowserApplication::pointer_press(float x,float y,PointerButton b,ResourceLoader&l){if(b!=PointerButton::Primary)return {};m_primary_pressed=true;auto*s=active_session();if(!s)return {NavigationError::InvalidUrl,"No active tab"};if(s->activate_control_at(x,y))return {};return s->activate_at(x,y,l);}
bool BrowserApplication::pointer_release(float,float,PointerButton b){if(b!=PointerButton::Primary)return false;bool was=m_primary_pressed;m_primary_pressed=false;return was;}
bool BrowserApplication::text_input(std::string const&text){auto*s=active_session();auto*f=s?s->form_runtime():nullptr;if(!f)return false;for(size_t i=0;i<f->control_count();++i){auto c=f->control(i);if(c&&c->focused)return const_cast<FormRuntime*>(f)->insert_text(text);}return false;}
bool BrowserApplication::backspace(){auto*s=active_session();auto*f=s?s->form_runtime():nullptr;if(!f)return false;for(size_t i=0;i<f->control_count();++i){auto c=f->control(i);if(c&&c->focused)return const_cast<FormRuntime*>(f)->backspace();}return false;}
void BrowserApplication::resize(float w,float h){m_width=w;m_height=h;for(auto&t:m_tabs)t.session.set_viewport(w,h);}void BrowserApplication::scroll(float dx,float dy){if(auto*s=active_session())s->scroll_by(dx,dy);}
}
