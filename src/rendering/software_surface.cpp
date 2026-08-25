#include "software_surface.hpp"

#include <algorithm>
#include <cmath>

namespace aetheris::rendering {

SoftwareSurface::SoftwareSurface(int width, int height, Color clear_color)
    : m_width(std::max(width, 0))
    , m_height(std::max(height, 0))
    , m_pixels(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), clear_color.rgba())
{
}

void SoftwareSurface::clear(Color color)
{
    std::fill(m_pixels.begin(), m_pixels.end(), color.rgba());
}

bool SoftwareSurface::contains(int x, int y) const
{
    return x >= 0 && y >= 0 && x < m_width && y < m_height;
}

void SoftwareSurface::blend_pixel(int x, int y, Color color)
{
    if (!contains(x, y))
        return;
    auto index = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
    auto old = m_pixels[index];
    Color destination {
        static_cast<uint8_t>(old >> 24),
        static_cast<uint8_t>(old >> 16),
        static_cast<uint8_t>(old >> 8),
        static_cast<uint8_t>(old),
    };
    m_pixels[index] = Color::blend_over(color, destination).rgba();
}

Color SoftwareSurface::pixel(int x, int y) const
{
    if (!contains(x, y))
        return {};
    auto value = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    return {
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
    };
}

std::optional<LayoutRect> SoftwareSurface::clipped(LayoutRect const& rect, std::optional<RoundedRect> const& clip)
{
    if (!clip)
        return rect;
    auto result = intersect_rect(rect, clip->rect);
    if (result.width <= 0 || result.height <= 0)
        return {};
    return result;
}

bool SoftwareSurface::accepts(int x, int y, std::optional<RoundedRect> const& clip) const
{
    return !clip || clip->contains(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
}

void SoftwareSurface::fill_rect(LayoutRect const& source_rect, Color color, std::optional<RoundedRect> clip)
{
    auto clipped_rect = clipped(source_rect, clip);
    if (!clipped_rect)
        return;

    auto const& rect = *clipped_rect;
    auto left = std::clamp(static_cast<int>(std::floor(rect.x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(rect.y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, m_height);

    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (accepts(x, y, clip))
                blend_pixel(x, y, color);
}

void SoftwareSurface::fill_rounded_rect(LayoutRect const& rect, float radius, Color color, std::optional<RoundedRect> clip)
{
    RoundedRect shape { rect, radius };
    auto bounds = clipped(rect, clip);
    if (!bounds)
        return;

    auto left = std::clamp(static_cast<int>(std::floor(bounds->x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(bounds->y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(bounds->x + bounds->width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(bounds->y + bounds->height)), 0, m_height);

    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (shape.contains(x + 0.5f, y + 0.5f) && accepts(x, y, clip))
                blend_pixel(x, y, color);
}

void SoftwareSurface::stroke_rect(LayoutRect const& rect, BoxEdges const& edges, Color color, std::optional<RoundedRect> clip, std::string_view style)
{
    auto paint = [&](LayoutRect const& piece, int offset) {
        if (style == "dashed" || style == "dotted") {
            int step = style == "dashed" ? 6 : 3;
            for (int y = static_cast<int>(piece.y); y < static_cast<int>(piece.y + piece.height); ++y)
                for (int x = static_cast<int>(piece.x); x < static_cast<int>(piece.x + piece.width); ++x)
                    if (((x + y + offset) / step) % 2 == 0 && accepts(x, y, clip))
                        blend_pixel(x, y, color);
            return;
        }
        fill_rect(piece, color, clip);
    };

    if (edges.top > 0) paint({ rect.x, rect.y, rect.width, edges.top }, 0);
    if (edges.bottom > 0) paint({ rect.x, rect.y + rect.height - edges.bottom, rect.width, edges.bottom }, 1);
    if (edges.left > 0) paint({ rect.x, rect.y, edges.left, rect.height }, 2);
    if (edges.right > 0) paint({ rect.x + rect.width - edges.right, rect.y, edges.right, rect.height }, 3);
}

void SoftwareSurface::stroke_rounded_rect(LayoutRect const& rect, float radius, BoxEdges const& edges, Color color, std::optional<RoundedRect> clip, std::string_view style)
{
    float width = std::max({ edges.top, edges.right, edges.bottom, edges.left });
    if (width <= 0)
        return;

    RoundedRect outer { rect, radius };
    LayoutRect inner_rect {
        rect.x + width,
        rect.y + width,
        std::max(0.0f, rect.width - 2 * width),
        std::max(0.0f, rect.height - 2 * width)
    };
    RoundedRect inner { inner_rect, std::max(0.0f, radius - width) };

    auto left = std::clamp(static_cast<int>(std::floor(rect.x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(rect.y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, m_height);

    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            bool dash = style == "solid" || (((x + y) / (style == "dashed" ? 6 : 3)) % 2 == 0);
            if (dash && outer.contains(x + 0.5f, y + 0.5f) && !inner.contains(x + 0.5f, y + 0.5f) && accepts(x, y, clip))
                blend_pixel(x, y, color);
        }
    }
}

void SoftwareSurface::fill_text_cell(LayoutRect const& rect, Color color, std::optional<RoundedRect> clip)
{
    auto glyph = rect;
    glyph.x += 1;
    glyph.y += 1;
    glyph.width = std::max(0.0f, glyph.width - 2);
    glyph.height = std::max(0.0f, glyph.height * 0.7f - 1);
    fill_rect(glyph, color, clip);
}

void SoftwareSurface::draw_image(LayoutRect const& rect, Image const& image, std::optional<RoundedRect> clip)
{
    if (!image.valid() || rect.width <= 0 || rect.height <= 0) return;
    auto left = std::max(0, static_cast<int>(std::floor(rect.x))); auto top = std::max(0, static_cast<int>(std::floor(rect.y)));
    auto right = std::min(m_width, static_cast<int>(std::ceil(rect.x + rect.width))); auto bottom = std::min(m_height, static_cast<int>(std::ceil(rect.y + rect.height)));
    for (int y=top;y<bottom;++y) for(int x=left;x<right;++x) { if(!accepts(x,y,clip)) continue; int sx=std::clamp(int((x-rect.x)*image.width/rect.width),0,image.width-1); int sy=std::clamp(int((y-rect.y)*image.height/rect.height),0,image.height-1); blend_pixel(x,y,image.pixel(sx,sy)); }
}

} // namespace aetheris::rendering

namespace aetheris::rendering {
void SoftwareSurface::draw_image(LayoutRect const& rect, Image const& image, std::string_view object_fit, std::optional<RoundedRect> clip)
{
    if (!image.valid() || rect.width <= 0 || rect.height <= 0) return;
    LayoutRect target=rect;
    float iw=float(image.width), ih=float(image.height), rw=rect.width, rh=rect.height;
    if (object_fit == "contain" || object_fit == "cover") { float scale=(object_fit=="contain")?std::min(rw/iw,rh/ih):std::max(rw/iw,rh/ih); target.width=iw*scale; target.height=ih*scale; target.x=rect.x+(rw-target.width)/2; target.y=rect.y+(rh-target.height)/2; }
    int left=std::max(0,int(std::floor(target.x))), top=std::max(0,int(std::floor(target.y))), right=std::min(m_width,int(std::ceil(target.x+target.width))), bottom=std::min(m_height,int(std::ceil(target.y+target.height)));
    for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) { if(!accepts(x,y,clip)) continue; int sx=std::clamp(int((x-target.x)*image.width/target.width),0,image.width-1); int sy=std::clamp(int((y-target.y)*image.height/target.height),0,image.height-1); blend_pixel(x,y,image.pixel(sx,sy)); }
}
void SoftwareSurface::draw_shadow(LayoutRect const& rect,float ox,float oy,float blur,Color color,std::optional<RoundedRect> clip)
{
    int spread=std::max(0,int(std::ceil(blur))); LayoutRect shadow{rect.x+ox-spread,rect.y+oy-spread,rect.width+2*spread,rect.height+2*spread}; color.alpha=uint8_t(color.alpha/std::max(1,spread+1)); fill_rect(shadow,color,clip);
}
void SoftwareSurface::draw_background_image(LayoutRect const& rect, Image const& image, std::string_view repeat, std::string_view position, std::string_view size, std::optional<RoundedRect> clip)
{
    if (!image.valid() || rect.width <= 0 || rect.height <= 0) return;
    float tile_w = static_cast<float>(image.width), tile_h = static_cast<float>(image.height);
    if (size == "cover" || size == "contain") {
        float scale = size == "cover" ? std::max(rect.width/tile_w, rect.height/tile_h) : std::min(rect.width/tile_w, rect.height/tile_h);
        tile_w *= scale; tile_h *= scale;
    } else if (size == "100% 100%") { tile_w = rect.width; tile_h = rect.height; }
    float ox = rect.x, oy = rect.y;
    if (position.find("center") != std::string_view::npos) { ox += (rect.width-tile_w)/2; oy += (rect.height-tile_h)/2; }
    else if (position.find("right") != std::string_view::npos) ox += rect.width-tile_w;
    else if (position.find("bottom") != std::string_view::npos) oy += rect.height-tile_h;
    int x_count = (repeat == "repeat" || repeat == "repeat-x") ? static_cast<int>(std::ceil(rect.width/tile_w))+2 : 1;
    int y_count = (repeat == "repeat" || repeat == "repeat-y") ? static_cast<int>(std::ceil(rect.height/tile_h))+2 : 1;
    if (repeat == "no-repeat") { x_count=1; y_count=1; }
    auto effective_clip = clip.value_or(RoundedRect{rect,0});
    for (int yi=0; yi<y_count; ++yi)
        for (int xi=0; xi<x_count; ++xi) {
            LayoutRect tile{ox + xi*tile_w, oy + yi*tile_h, tile_w, tile_h};
            draw_image(tile, image, "fill", effective_clip);
        }
}
}
