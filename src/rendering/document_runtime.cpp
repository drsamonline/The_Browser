#include "document_runtime.hpp"
#include <algorithm>
namespace aetheris::rendering {
DocumentRuntime::DocumentRuntime(Document& d):m_document(d){}
void DocumentRuntime::add_event_listener(DomNode* n,DomEventType t,Listener l){if(n&&l)m_listeners[n][static_cast<int>(t)].push_back(std::move(l));}
DomEvent DocumentRuntime::dispatch(DomEventType t,DomNode* target){DomEvent e{t,target}; for(auto* n=target;n;n=n->parent){auto ni=m_listeners.find(n);if(ni==m_listeners.end())continue;auto ti=ni->second.find(static_cast<int>(t));if(ti==ni->second.end())continue;for(auto& l:ti->second){l(e);if(e.default_prevented)return e;}} return e;}
bool DocumentRuntime::set_attribute(DomNode* n,std::string k,std::string v){if(!n||n->type!=DomNodeType::Element)return false;n->attributes[std::move(k)]=std::move(v);mark_dirty();return true;}
bool DocumentRuntime::set_text(DomNode* n,std::string v){if(!n||n->type!=DomNodeType::Text)return false;n->data=std::move(v);mark_dirty();return true;}
DomNode* DocumentRuntime::append_element(DomNode* p,std::string name){if(!p)return nullptr;auto n=std::make_unique<DomNode>(DomNodeType::Element,std::move(name));auto& ref=p->append_child(std::move(n));mark_dirty();return &ref;}
bool DocumentRuntime::remove(DomNode* n){if(!n||!n->parent)return false;auto& v=n->parent->children;auto it=std::find_if(v.begin(),v.end(),[&](auto const& p){return p.get()==n;});if(it==v.end())return false;v.erase(it);mark_dirty();return true;}
void DocumentRuntime::mark_dirty(){m_dirty=true;}
}
