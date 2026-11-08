#include "glyph_rasterizer.hpp"

#include <algorithm>
#include <cmath>

namespace aetheris::rendering {

namespace {

struct Edge {
    float x0 { 0 };
    float y0 { 0 };
    float x1 { 0 };
    float y1 { 0 };
};

struct Point {
    float x { 0 };
    float y { 0 };
};

// Quadratic Bezier sampling. Eight steps is enough at text sizes and keeps the
// edge list small.
void flatten_quadratic(std::vector<Point>& out, Point from, Point control, Point to)
{
    constexpr int kSteps = 8;
    for (int step = 1; step <= kSteps; ++step) {
        float t = static_cast<float>(step) / static_cast<float>(kSteps);
        float inv = 1.0f - t;
        Point point;
        point.x = inv * inv * from.x + 2.0f * inv * t * control.x + t * t * to.x;
        point.y = inv * inv * from.y + 2.0f * inv * t * control.y + t * t * to.y;
        out.push_back(point);
    }
}

// Turns a contour (on/off-curve points, as stored in glyf) into a polyline.
std::vector<Point> contour_polyline(TtfContour const& contour, float scale)
{
    std::vector<Point> points;
    if (contour.points.size() < 2)
        return points;

    // TrueType allows two consecutive off-curve points: the on-curve point
    // between them is implied at their midpoint.
    std::vector<Point> expanded;
    std::vector<bool> on_curve;
    for (std::size_t index = 0; index < contour.points.size(); ++index) {
        auto const& current = contour.points[index];
        expanded.push_back(Point { current.x * scale, -current.y * scale });
        on_curve.push_back(current.on_curve);
    }
    // Insert implied midpoints.
    std::vector<Point> final_points;
    std::vector<bool> final_on;
    for (std::size_t index = 0; index < expanded.size(); ++index) {
        auto next = (index + 1) % expanded.size();
        final_points.push_back(expanded[index]);
        final_on.push_back(on_curve[index]);
        if (!on_curve[index] && !on_curve[next]) {
            final_points.push_back(Point { (expanded[index].x + expanded[next].x) * 0.5f,
                (expanded[index].y + expanded[next].y) * 0.5f });
            final_on.push_back(true);
        }
    }

    // Start at an on-curve point.
    std::size_t start = 0;
    for (std::size_t index = 0; index < final_points.size(); ++index) {
        if (final_on[index]) {
            start = index;
            break;
        }
    }

    points.push_back(final_points[start]);
    std::size_t count = final_points.size();
    std::size_t index = (start + 1) % count;
    Point control {};
    bool have_control = false;
    for (std::size_t step = 0; step < count; ++step, index = (index + 1) % count) {
        if (final_on[index]) {
            if (have_control) {
                flatten_quadratic(points, points.back(), control, final_points[index]);
                have_control = false;
            } else {
                points.push_back(final_points[index]);
            }
        } else {
            if (have_control) {
                Point midpoint { (control.x + final_points[index].x) * 0.5f,
                    (control.y + final_points[index].y) * 0.5f };
                flatten_quadratic(points, points.back(), control, midpoint);
            }
            control = final_points[index];
            have_control = true;
        }
    }
    if (have_control)
        flatten_quadratic(points, points.back(), control, points.front());
    return points;
}

} // namespace

GlyphBitmap rasterize_glyph(TtfFont const& font, uint16_t glyph, float pixel_size)
{
    GlyphBitmap bitmap;
    if (!font.loaded() || pixel_size <= 0.0f)
        return bitmap;

    auto outline = font.outline(glyph);
    if (outline.empty())
        return bitmap;

    float scale = pixel_size / static_cast<float>(font.units_per_em() > 0 ? font.units_per_em() : 1000);

    std::vector<Edge> edges;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    bool first = true;

    for (auto const& contour : outline.contours) {
        auto points = contour_polyline(contour, scale);
        if (points.size() < 3)
            continue;
        for (std::size_t index = 0; index < points.size(); ++index) {
            auto const& a = points[index];
            auto const& b = points[(index + 1) % points.size()];
            if (first) {
                min_x = max_x = a.x;
                min_y = max_y = a.y;
                first = false;
            }
            min_x = std::min({ min_x, a.x, b.x });
            max_x = std::max({ max_x, a.x, b.x });
            min_y = std::min({ min_y, a.y, b.y });
            max_y = std::max({ max_y, a.y, b.y });
            if (a.y == b.y)
                continue; // horizontal edges do not contribute to crossings
            edges.push_back(Edge { a.x, a.y, b.x, b.y });
        }
    }
    if (edges.empty())
        return bitmap;

    // One pixel of padding so anti-aliased edges are not clipped.
    int left = static_cast<int>(std::floor(min_x)) - 1;
    int top = static_cast<int>(std::floor(min_y)) - 1;
    int width = static_cast<int>(std::ceil(max_x)) - left + 1;
    int height = static_cast<int>(std::ceil(max_y)) - top + 1;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return bitmap;

    bitmap.width = width;
    bitmap.height = height;
    bitmap.left = left;
    bitmap.top = top;
    bitmap.coverage.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    // Accumulate in float and round once: truncating per sub-sample loses up to
    // a full level each time, which leaves solid interiors at 252 instead of 255.
    std::vector<float> accumulator(bitmap.coverage.size(), 0.0f);

    // Vertical supersampling: four sub-scanlines per pixel row, with exact
    // horizontal coverage inside each span.
    constexpr int kSubSamples = 4;
    std::vector<float> crossings;
    for (int row = 0; row < height; ++row) {
        for (int sample = 0; sample < kSubSamples; ++sample) {
            float y = static_cast<float>(top + row) + (static_cast<float>(sample) + 0.5f) / kSubSamples;
            crossings.clear();
            for (auto const& edge : edges) {
                float low = std::min(edge.y0, edge.y1);
                float high = std::max(edge.y0, edge.y1);
                if (y < low || y >= high)
                    continue;
                float t = (y - edge.y0) / (edge.y1 - edge.y0);
                crossings.push_back(edge.x0 + t * (edge.x1 - edge.x0));
            }
            if (crossings.size() < 2)
                continue;
            std::sort(crossings.begin(), crossings.end());

            for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
                float span_start = crossings[index] - static_cast<float>(left);
                float span_end = crossings[index + 1] - static_cast<float>(left);
                if (span_end <= span_start)
                    continue;
                int first_pixel = std::max(0, static_cast<int>(std::floor(span_start)));
                int last_pixel = std::min(width - 1, static_cast<int>(std::ceil(span_end)) - 1);
                for (int column = first_pixel; column <= last_pixel; ++column) {
                    float pixel_start = static_cast<float>(column);
                    float pixel_end = pixel_start + 1.0f;
                    float covered = std::min(span_end, pixel_end) - std::max(span_start, pixel_start);
                    if (covered <= 0.0f)
                        continue;
                    auto& cell = accumulator[static_cast<std::size_t>(row) * static_cast<std::size_t>(width)
                        + static_cast<std::size_t>(column)];
                    cell += covered * (255.0f / static_cast<float>(kSubSamples));
                }
            }
        }
    }
    for (std::size_t index = 0; index < accumulator.size(); ++index)
        bitmap.coverage[index] = static_cast<uint8_t>(std::lround(std::min(255.0f, accumulator[index])));
    return bitmap;
}

} // namespace aetheris::rendering
