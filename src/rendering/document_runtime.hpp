#pragma once
#include "document.hpp"
#include "visual_state.hpp"
#include <functional>
#include <string>
#include <unordered_map>
namespace aetheris::rendering {
enum class DomEventType { Click, Input, Change, Submit, Load };
struct DomEvent { DomEventType type; DomNode* target { nullptr }; bool default_prevented { false }; void prevent_default(){default_prevented=true;} };
class DocumentRuntime {
public:
    using Listener=std::function<void(DomEvent&)>;
    explicit DocumentRuntime(Document&);
    void add_event_listener(DomNode*,DomEventType,Listener);
    DomEvent dispatch(DomEventType,DomNode*);
    bool set_attribute(DomNode*,std::string,std::string);
    bool set_text(DomNode*,std::string);
    DomNode* append_element(DomNode*,std::string);
    bool remove(DomNode*);
    bool dirty() const{return m_dirty;} bool consume_dirty(){bool d=m_dirty;m_dirty=false;return d;}
private:
    Document& m_document; bool m_dirty{false}; std::unordered_map<DomNode*,std::unordered_map<int,std::vector<Listener>>> m_listeners;
    void mark_dirty();
};
}
