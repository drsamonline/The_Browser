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
    auto const* document = m_navigation.current_document();
    if (document) {
        auto const& rect = document->layout_root().rect;
        m_viewport.set_document_size(rect.width, rect.height);
        if (m_viewport.width() == 0)
            m_viewport.set_size(request.viewport_width, rect.height);
    }
    return result;
}

NavigationResult BrowserSession::activate_at(float viewport_x, float viewport_y, ResourceLoader& loader)
{
    auto const* document = m_navigation.current_document();
    if (!document)
        return { NavigationError::InvalidUrl, "No document is available for interaction" };

    auto hit = m_interaction.hit_test(*document, m_viewport, viewport_x, viewport_y);
    if (!hit.is_link())
        return { NavigationError::InvalidUrl, "No navigable hyperlink was activated" };

    auto destination = *hit.link_url;
    if (destination.has_fragment()) {
        auto base = m_navigation.current_url().serialized();
        if (auto marker = base.find('#'); marker != std::string::npos)
            base.erase(marker);
        auto target_base = destination.serialized();
        if (auto marker = target_base.find('#'); marker != std::string::npos)
            target_base.erase(marker);
        if (target_base == base) {
            if (!scroll_to_fragment(destination.fragment()))
                return { NavigationError::InvalidUrl, "Fragment target was not found" };
            return {};
        }
    }

    NavigationRequest request { destination, std::nullopt, m_viewport.width() };
    return navigate(request, loader);
}

bool BrowserSession::scroll_to_fragment(std::string const& fragment)
{
    auto const* document = m_navigation.current_document();
    if (!document)
        return false;
    auto target = m_interaction.fragment_target(*document, fragment);
    if (!target)
        return false;
    m_viewport.scroll_to(target->x, target->y);
    return true;
}

void BrowserSession::set_viewport(float width, float height)
{
    m_viewport.set_size(width, height);
    if (auto const* document = m_navigation.current_document()) {
        auto const& rect = document->layout_root().rect;
        m_viewport.set_document_size(rect.width, rect.height);
    }
}

void BrowserSession::scroll_by(float dx, float dy)
{
    m_viewport.scroll_by(dx, dy);
}

void BrowserSession::scroll_to(float x, float y)
{
    m_viewport.scroll_to(x, y);
}

HitTestResult BrowserSession::hit_test(float viewport_x, float viewport_y) const
{
    auto const* document = m_navigation.current_document();
    if (!document)
        return {};
    return m_interaction.hit_test(*document, m_viewport, viewport_x, viewport_y);
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
