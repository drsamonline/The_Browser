#include "resource.hpp"
namespace aetheris::rendering {
void ResourceCache::put(ResourceData d){ auto p=std::make_shared<ResourceData>(std::move(d)); m_resources[p->url]=std::move(p); }
std::shared_ptr<ResourceData const> ResourceCache::get(std::string const& u) const { auto it=m_resources.find(u); return it==m_resources.end()?nullptr:it->second; }
bool ResourceCache::contains(std::string const& u) const { return m_resources.contains(u); }
void ResourceCache::clear(){m_resources.clear();}
}
