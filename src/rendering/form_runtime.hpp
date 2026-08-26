#pragma once

#include "document.hpp"
#include "url.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aetheris::rendering {

enum class FormControlKind { Text, Password, Checkbox, Radio, Submit, Button, TextArea, Select };

struct FormControlState {
    DomNode const* node { nullptr };
    FormControlKind kind { FormControlKind::Text };
    std::string name;
    std::string value;
    bool checked { false };
    bool disabled { false };
    bool read_only { false };
    bool focused { false };
    std::vector<std::string> options;
};

struct FormSubmission {
    enum class Method { Get, Post };
    Method method { Method::Get };
    Url action;
    std::string encoded_body;
};

class FormRuntime {
public:
    explicit FormRuntime(Document const& document);

    size_t control_count() const { return m_controls.size(); }
    std::optional<size_t> focused_control() const { return m_focused; }
    FormControlState const* control(size_t index) const;

    bool focus(size_t index);
    bool clear_focus();
    bool insert_text(std::string_view text);
    bool backspace();
    bool set_value(size_t index, std::string value);
    bool activate(size_t index);

    std::optional<FormSubmission> submit(size_t submit_control_index, Url const& document_url) const;

private:
    void discover(DomNode const& node);
    static std::optional<FormControlKind> kind_for(DomNode const& node);
    static std::string url_encode(std::string_view value);
    static std::string lower(std::string value);
    static bool is_successful(FormControlState const& control);
    std::optional<size_t> form_owner(size_t control_index) const;
    std::vector<std::pair<std::string, std::string>> collect_fields(size_t control_index) const;

    Document const& m_document;
    std::vector<FormControlState> m_controls;
    std::vector<DomNode const*> m_forms;
    std::vector<std::optional<size_t>> m_control_forms;
    std::optional<size_t> m_focused;
};

} // namespace aetheris::rendering
