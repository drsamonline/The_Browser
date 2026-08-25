#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aetheris::rendering {

enum class ResourceType {
    Document,
    Stylesheet,
    Image,
    Font,
    Binary,
};

struct ResourceData {
    ResourceType type;
    std::string url;
    std::vector<unsigned char> bytes;
};

class ResourceCache {
public:
    void put(ResourceData);
    void put_text(ResourceType, std::string url, std::string_view text);

    std::shared_ptr<ResourceData const> get(std::string const&) const;
    std::string get_text(std::string const&) const;

    bool contains(std::string const&) const;
    size_t size() const { return m_resources.size(); }
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<ResourceData>> m_resources;
};

} // namespace aetheris::rendering
