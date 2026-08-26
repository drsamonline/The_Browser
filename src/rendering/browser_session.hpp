#pragma once

#include "navigation.hpp"
#include "document_interaction.hpp"
#include "viewport.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace aetheris::rendering {

struct PageMetadata {
    Url url;
    std::string title;
    PageLifecycleState lifecycle;
};

struct HistoryEntry {
    NavigationRequest request;
    PageMetadata metadata;
};

class BrowserSession {
public:
    NavigationResult navigate(NavigationRequest const&, ResourceLoader&);
    NavigationResult reload(ResourceLoader&);
    NavigationResult back(ResourceLoader&);
    NavigationResult forward(ResourceLoader&);

    NavigationResult activate_at(float viewport_x, float viewport_y, ResourceLoader&);
    bool scroll_to_fragment(std::string const& fragment);
    void set_viewport(float width, float height);
    void scroll_by(float dx, float dy);
    void scroll_to(float x, float y);
    Viewport const& viewport() const { return m_viewport; }
    HitTestResult hit_test(float viewport_x, float viewport_y) const;

    bool can_go_back() const;
    bool can_go_forward() const;
    std::size_t history_size() const { return m_history.size(); }
    std::optional<std::size_t> current_history_index() const { return m_current_history_index; }

    NavigationController const& navigation() const { return m_navigation; }
    PageMetadata const* current_page() const;
    std::vector<HistoryEntry> const& history() const { return m_history; }

private:
    NavigationResult commit(NavigationRequest const&, ResourceLoader&, bool append_history);
    static std::string extract_title(ResourceLoader&, Url const&);

    NavigationController m_navigation;
    std::vector<HistoryEntry> m_history;
    std::optional<std::size_t> m_current_history_index;
    std::optional<PageMetadata> m_current_page;
    Viewport m_viewport;
    DocumentInteraction m_interaction;
};

} // namespace aetheris::rendering
