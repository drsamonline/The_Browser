#pragma once
#include "style.hpp"
#include <string_view>
namespace aetheris::rendering { struct FontMetrics { float size{16}; float ascent{12.8f}; float descent{3.2f}; float line_gap{3.2f}; float advance(char) const; float line_height() const{return ascent+descent+line_gap;} }; class Font { public: static Font from_style(StyleProperties const&); FontMetrics const& metrics() const{return m_metrics;} float measure(std::string_view) const; private: FontMetrics m_metrics; float m_weight{400}; }; }
