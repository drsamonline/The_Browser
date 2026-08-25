#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace aetheris::rendering {
enum class ResourceType { Image, Font };
struct ResourceData { ResourceType type; std::string url; std::vector<unsigned char> bytes; };
class ResourceCache {
public:
 void put(ResourceData); std::shared_ptr<ResourceData const> get(std::string const&) const;
 bool contains(std::string const&) const; void clear();
private: std::unordered_map<std::string,std::shared_ptr<ResourceData>> m_resources;
};
} // namespace aetheris::rendering
