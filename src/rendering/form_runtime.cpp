#include "form_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace aetheris::rendering {

static std::string attribute_or(DomNode const& node, std::string const& name, std::string fallback = {}) {
    if (auto const* value = node.attribute(name); value)
        return *value;
    return fallback;
}

FormRuntime::FormRuntime(Document const& document)
    : m_document(document) {
    discover(m_document.root());
}

FormControlState const* FormRuntime::control(size_t index) const {
    return index < m_controls.size() ? &m_controls[index] : nullptr;
}

std::string FormRuntime::lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<FormControlKind> FormRuntime::kind_for(DomNode const& node) {
    if (node.type != DomNodeType::Element)
        return {};
    auto name = lower(node.name);
    if (name == "textarea") return FormControlKind::TextArea;
    if (name == "select") return FormControlKind::Select;
    if (name == "button") {
        auto type = lower(attribute_or(node, "type", "submit"));
        return type == "submit" ? FormControlKind::Submit : FormControlKind::Button;
    }
    if (name != "input") return {};
    auto type = lower(attribute_or(node, "type", "text"));
    if (type == "password") return FormControlKind::Password;
    if (type == "checkbox") return FormControlKind::Checkbox;
    if (type == "radio") return FormControlKind::Radio;
    if (type == "submit") return FormControlKind::Submit;
    if (type == "button") return FormControlKind::Button;
    return FormControlKind::Text;
}

void FormRuntime::discover(DomNode const& node) {
    size_t form_count_before = m_forms.size();
    bool is_form = node.type == DomNodeType::Element && lower(node.name) == "form";
    if (is_form)
        m_forms.push_back(&node);

    if (auto kind = kind_for(node); kind.has_value()) {
        FormControlState state;
        state.node = &node;
        state.kind = *kind;
        state.name = attribute_or(node, "name");
        state.value = attribute_or(node, "value");
        state.checked = node.has_attribute("checked");
        state.disabled = node.has_attribute("disabled");
        state.read_only = node.has_attribute("readonly");
        if (state.kind == FormControlKind::TextArea)
            state.value = node.data;
        if (state.kind == FormControlKind::Select) {
            for (auto const& child : node.children) {
                if (child && child->type == DomNodeType::Element && lower(child->name) == "option") {
                    auto value = attribute_or(*child, "value", child->data);
                    state.options.push_back(value);
                    if (child->has_attribute("selected")) state.value = value;
                }
            }
            if (state.value.empty() && !state.options.empty()) state.value = state.options.front();
        }
        m_controls.push_back(std::move(state));
        m_control_forms.push_back(m_forms.empty() ? std::nullopt : std::optional<size_t>(m_forms.size() - 1));
    }

    for (auto const& child : node.children)
        if (child) discover(*child);

    if (is_form && m_forms.size() > form_count_before)
        m_forms.resize(form_count_before + 1);
}

bool FormRuntime::focus(size_t index) {
    if (index >= m_controls.size() || m_controls[index].disabled) return false;
    if (m_focused) m_controls[*m_focused].focused = false;
    m_focused = index;
    m_controls[index].focused = true;
    return true;
}

bool FormRuntime::clear_focus() {
    if (!m_focused) return false;
    m_controls[*m_focused].focused = false;
    m_focused.reset();
    return true;
}

bool FormRuntime::insert_text(std::string_view text) {
    if (!m_focused) return false;
    auto& control = m_controls[*m_focused];
    if (control.disabled || control.read_only || (control.kind != FormControlKind::Text && control.kind != FormControlKind::Password && control.kind != FormControlKind::TextArea)) return false;
    control.value.append(text);
    return true;
}

bool FormRuntime::backspace() {
    if (!m_focused) return false;
    auto& control = m_controls[*m_focused];
    if (control.disabled || control.read_only || control.value.empty()) return false;
    if (control.kind != FormControlKind::Text && control.kind != FormControlKind::Password && control.kind != FormControlKind::TextArea) return false;
    control.value.pop_back();
    return true;
}

bool FormRuntime::set_value(size_t index, std::string value) {
    if (index >= m_controls.size()) return false;
    auto& control = m_controls[index];
    if (control.disabled || control.read_only) return false;
    if (control.kind == FormControlKind::Select && std::find(control.options.begin(), control.options.end(), value) == control.options.end()) return false;
    control.value = std::move(value);
    return true;
}

bool FormRuntime::activate(size_t index) {
    if (index >= m_controls.size() || m_controls[index].disabled) return false;
    auto& control = m_controls[index];
    if (control.kind == FormControlKind::Checkbox) { control.checked = !control.checked; return true; }
    if (control.kind == FormControlKind::Radio) {
        auto owner = form_owner(index);
        for (size_t i = 0; i < m_controls.size(); ++i) {
            if (i == index || m_controls[i].kind != FormControlKind::Radio || m_controls[i].name != control.name) continue;
            if (form_owner(i) == owner) m_controls[i].checked = false;
        }
        control.checked = true;
        return true;
    }
    return control.kind == FormControlKind::Submit || control.kind == FormControlKind::Button;
}

std::optional<size_t> FormRuntime::form_owner(size_t control_index) const {
    if (control_index >= m_control_forms.size()) return {};
    return m_control_forms[control_index];
}

bool FormRuntime::is_successful(FormControlState const& control) {
    if (control.disabled || control.name.empty()) return false;
    if ((control.kind == FormControlKind::Checkbox || control.kind == FormControlKind::Radio) && !control.checked) return false;
    return control.kind != FormControlKind::Button && control.kind != FormControlKind::Submit;
}

std::vector<std::pair<std::string, std::string>> FormRuntime::collect_fields(size_t control_index) const {
    std::vector<std::pair<std::string, std::string>> fields;
    auto owner = form_owner(control_index);
    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (form_owner(i) != owner || !is_successful(m_controls[i])) continue;
        fields.emplace_back(m_controls[i].name, m_controls[i].value);
    }
    return fields;
}

std::string FormRuntime::url_encode(std::string_view value) {
    std::ostringstream out;
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out << ch;
        else if (ch == ' ') out << '+';
        else out << '%' << hex[ch >> 4] << hex[ch & 15];
    }
    return out.str();
}

std::optional<FormSubmission> FormRuntime::submit(size_t submit_control_index, Url const& document_url) const {
    if (submit_control_index >= m_controls.size()) return {};
    auto const& submit = m_controls[submit_control_index];
    if (submit.disabled || (submit.kind != FormControlKind::Submit && submit.kind != FormControlKind::Button)) return {};
    auto owner = form_owner(submit_control_index);
    Url action = document_url;
    FormSubmission::Method method = FormSubmission::Method::Get;
    if (owner && *owner < m_forms.size()) {
        auto const& form = *m_forms[*owner];
        if (auto const* raw_action = form.attribute("action"); raw_action && !raw_action->empty()) action = Url::resolve(document_url, *raw_action);
        if (auto const* raw_method = form.attribute("method"); raw_method && lower(*raw_method) == "post") method = FormSubmission::Method::Post;
    }
    std::ostringstream encoded;
    bool first = true;
    for (auto const& [name, value] : collect_fields(submit_control_index)) {
        if (!first) encoded << '&';
        first = false;
        encoded << url_encode(name) << '=' << url_encode(value);
    }
    FormSubmission result { method, std::move(action), encoded.str() };
    return result;
}

} // namespace aetheris::rendering
