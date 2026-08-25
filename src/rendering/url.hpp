#pragma once

#include <string>
#include <string_view>

namespace aetheris::rendering {

class Url {
public:
    Url() = default;
    explicit Url(std::string value);

    static Url parse(std::string_view);
    static Url resolve(Url const& base, std::string_view reference);

    std::string const& serialized() const { return m_serialized; }
    std::string const& scheme() const { return m_scheme; }
    std::string const& authority() const { return m_authority; }
    std::string const& path() const { return m_path; }
    bool is_absolute() const { return !m_scheme.empty(); }
    bool is_valid() const { return !m_serialized.empty(); }

private:
    std::string m_serialized;
    std::string m_scheme;
    std::string m_authority;
    std::string m_path;

    void parse_components();
};

} // namespace aetheris::rendering
