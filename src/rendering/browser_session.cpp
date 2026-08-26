#include "browser_session.hpp"

#include "document.hpp"

#include <functional>

namespace aetheris::rendering {

NavigationResult BrowserSession::navigate(NavigationRequest const& request, ResourceLoader& loader)
{
    return commit(request, loader, true);
}

NavigationResult BrowserSession::reload(ResourceLoader& loader)
{
    if (!m_current_history_index)
        return { NavigationError::InvalidUrl, "No successful navigation is available to reload" };
    return commit(m_history[*m_current_history_index].request, loader, false);
}

NavigationResult BrowserSession::back(ResourceLoader& loader)
{
    if (!can_go_back())
        return { NavigationError::InvalidUrl, "No previous history entry is available" };
    auto target = *m_current_history_index - 1;
    auto result = commit(m_history[target].request, loader, false);
    if (result.succeeded())
        m_current_history_index = target;
    return result;
}

NavigationResult BrowserSession::forward(ResourceLoader& loader)
{
    if (!can_go_forward())
        return { NavigationError::InvalidUrl, "No forward history entry is available" };
    auto target = *m_current_history_index + 1;
    auto result = commit(m_history[target].request, loader, false);
    if (result.succeeded())
        m_current_history_index = target;
    return result;
}

bool BrowserSession::can_go_back() const
{
    return m_current_history_index && *m_current_history_index > 0;
}

bool BrowserSession::can_go_forward() const
{
    return m_current_history_index && *m_current_history_index + 1 < m_history.size();
}

PageMetadata const* BrowserSession::current_page() const
{
    return m_current_page ? &*m_current_page : nullptr;
}

NavigationResult BrowserSession::commit(NavigationRequest const& request, ResourceLoader& loader, bool append_history)
{
    auto result = m_navigation.navigate(request, loader);
    if (!result.succeeded())
        return result;

    PageMetadata metadata { m_navigation.current_url(), extract_title(loader, m_navigation.current_url()), m_navigation.lifecycle() };

    if (append_history) {
        if (m_current_history_index && *m_current_history_index + 1 < m_history.size())
            m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(*m_current_history_index + 1), m_history.end());
        m_history.push_back({ request, metadata });
        m_current_history_index = m_history.size() - 1;
    } else if (m_current_history_index) {
        m_history[*m_current_history_index].metadata = metadata;
    }

    m_current_page = std::move(metadata);
    return result;
}

std::string BrowserSession::extract_title(ResourceLoader& loader, Url const& url)
{
    auto source = loader.cache().get_text(url.serialized());
    if (source.empty())
        return {};

    auto document = Document::parse_html(source);
    std::string title;
    std::function<bool(DomNode const&)> walk = [&](DomNode const& node) {
        if (node.type == DomNodeType::Element && node.name == "title") {
            std::function<void(DomNode const&)> collect = [&](DomNode const& child) {
                if (child.type == DomNodeType::Text)
                    title += child.data;
                for (auto const& grandchild : child.children)
                    collect(*grandchild);
            };
            for (auto const& child : node.children)
                collect(*child);
            return true;
        }
        for (auto const& child : node.children) {
            if (walk(*child))
                return true;
        }
        return false;
    };
    walk(document.root());
    return title;
}

} // namespace aetheris::rendering
