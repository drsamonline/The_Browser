#pragma once
#include "color.hpp"
#include "resource.hpp"
#include <memory>
#include <string>
#include <vector>
namespace aetheris::rendering {
struct Image { int width{0}; int height{0}; std::vector<Color> pixels; bool valid() const{return width>0&&height>0&&pixels.size()==size_t(width)*size_t(height);} Color pixel(int x,int y) const; static std::shared_ptr<Image const> load_ppm(ResourceData const&); };
class ImageCache { public: std::shared_ptr<Image const> load(std::string const&, ResourceCache const&); void clear(); private: std::unordered_map<std::string,std::shared_ptr<Image const>> m_images; };
}
