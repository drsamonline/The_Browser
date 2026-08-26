#pragma once

#include "navigation.hpp"

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
};

} // namespace aetheris::rendering
