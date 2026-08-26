#include "url.hpp"

namespace aetheris::rendering {

Url::Url(std::string value)
    : m_serialized(std::move(value))
{
    parse_components();
}

Url Url::parse(std::string_view value)
{
    return Url(std::string(value));
}

Url Url::resolve(Url const& base, std::string_view reference)
{
    if (reference.empty())
        return base;

    auto ref = std::string(reference);
    if (ref.front() == '#') {
        auto serialized = base.serialized();
        if (auto existing = serialized.find('#'); existing != std::string::npos)
            serialized.erase(existing);
        return Url(serialized + ref);
    }
    if (auto scheme_end = ref.find(':'); scheme_end != std::string::npos
        && scheme_end > 0 && ref.find_first_of("/?#") > scheme_end)
        return Url(std::move(ref));

    if (ref.starts_with("//")) {
        if (base.scheme().empty())
            return Url(std::move(ref));
        return Url(base.scheme() + ":" + ref);
    }

    if (!base.is_absolute())
        return Url(std::move(ref));

    auto origin = base.scheme() + "://" + base.authority();
    if (ref.front() == '/')
        return Url(origin + ref);

    auto directory = base.path();
    auto slash = directory.rfind('/');
    directory = slash == std::string::npos ? "/" : directory.substr(0, slash + 1);

    std::string path = directory + ref;
    for (;;) {
        auto pos = path.find("/./");
        if (pos == std::string::npos)
            break;
        path.replace(pos, 3, "/");
    }
    for (;;) {
        auto pos = path.find("/../");
        if (pos == std::string::npos)
            break;
        auto previous = path.rfind('/', pos - 1);
        if (previous == std::string::npos)
            break;
        path.replace(previous, pos + 4 - previous, "/");
    }

    return Url(origin + path);
}

void Url::parse_components()
{
    m_scheme.clear();
    m_authority.clear();
    m_path.clear();
    m_fragment.clear();

    if (auto fragment_start = m_serialized.find('#'); fragment_start != std::string::npos)
        m_fragment = m_serialized.substr(fragment_start + 1);

    auto scheme_end = m_serialized.find("://");
    auto without_fragment = m_serialized.substr(0, m_serialized.find('#'));
    if (scheme_end == std::string::npos) {
        m_path = without_fragment;
        return;
    }

    m_scheme = m_serialized.substr(0, scheme_end);
    auto authority_start = scheme_end + 3;
    auto path_start = without_fragment.find('/', authority_start);
    if (path_start == std::string::npos) {
        m_authority = without_fragment.substr(authority_start);
        m_path = "/";
    } else {
        m_authority = without_fragment.substr(authority_start, path_start - authority_start);
        m_path = without_fragment.substr(path_start);
    }
}

} // namespace aetheris::rendering
