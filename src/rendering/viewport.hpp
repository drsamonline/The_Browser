#pragma once

#include "layout.hpp"

namespace aetheris::rendering {

struct ViewportPoint {
    float x { 0 };
    float y { 0 };
};

class Viewport {
public:
    Viewport(float width = 0, float height = 0)
        : m_width(width), m_height(height) { }

    float width() const { return m_width; }
    float height() const { return m_height; }
    float scroll_x() const { return m_scroll_x; }
    float scroll_y() const { return m_scroll_y; }

    void set_size(float width, float height);
    void set_document_size(float width, float height);
    void scroll_to(float x, float y);
    void scroll_by(float dx, float dy);

    ViewportPoint to_document_coordinates(float viewport_x, float viewport_y) const;
    ViewportPoint to_viewport_coordinates(float document_x, float document_y) const;

private:
    void clamp_scroll();

    float m_width { 0 };
    float m_height { 0 };
    float m_document_width { 0 };
    float m_document_height { 0 };
    float m_scroll_x { 0 };
    float m_scroll_y { 0 };
};

} // namespace aetheris::rendering
