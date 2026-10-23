#include "svg.hpp"

#include "color.hpp"
#include "html_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace aetheris::rendering {

namespace {

// ---------------------------------------------------------------------------
// Small SVG element tree
// ---------------------------------------------------------------------------

struct SvgNode {
    std::string tag;
    std::unordered_map<std::string, std::string> attributes; // keys lower-cased
    std::vector<SvgNode> children;
};

std::string to_lower(std::string value)
{
    for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string const* attr(SvgNode const& node, std::string const& name)
{
    auto it = node.attributes.find(name);
    return it == node.attributes.end() ? nullptr : &it->second;
}

double number(std::string const& text, double fallback = 0.0)
{
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

double attr_number(SvgNode const& node, std::string const& name, double fallback = 0.0)
{
    auto value = attr(node, name);
    return value ? number(*value, fallback) : fallback;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

struct Point {
    double x { 0 };
    double y { 0 };
};

using Polygon = std::vector<Point>;

struct Matrix {
    double a { 1 }, b { 0 }, c { 0 }, d { 1 }, e { 0 }, f { 0 };

    Point apply(Point p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
};

Matrix multiply(Matrix const& m, Matrix const& n)
{
    return {
        m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b,
        m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d,
        m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f
    };
}

Matrix parse_transform(std::string const& text)
{
    Matrix result;
    size_t pos = 0;
    while (pos < text.size()) {
        auto open = text.find('(', pos);
        if (open == std::string::npos)
            break;
        auto close = text.find(')', open);
        if (close == std::string::npos)
            break;
        std::string name = to_lower(text.substr(pos, open - pos));
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
        std::vector<double> args;
        std::string arg_text = text.substr(open + 1, close - open - 1);
        for (size_t i = 0; i < arg_text.size();) {
            if (std::isspace(static_cast<unsigned char>(arg_text[i])) || arg_text[i] == ',') { ++i; continue; }
            char* end = nullptr;
            double value = std::strtod(arg_text.c_str() + i, &end);
            if (end == arg_text.c_str() + i)
                break;
            args.push_back(value);
            i = static_cast<size_t>(end - arg_text.c_str());
        }
        Matrix m;
        if (name.find("translate") != std::string::npos && args.size() >= 1) {
            m.e = args[0];
            m.f = args.size() >= 2 ? args[1] : 0;
        } else if (name.find("scale") != std::string::npos && args.size() >= 1) {
            m.a = args[0];
            m.d = args.size() >= 2 ? args[1] : args[0];
        } else if (name.find("matrix") != std::string::npos && args.size() >= 6) {
            m = { args[0], args[1], args[2], args[3], args[4], args[5] };
        } else {
            pos = close + 1;
            continue;
        }
        result = multiply(result, m);
        pos = close + 1;
    }
    return result;
}

bool parse_paint(std::string const& text, Color& out)
{
    if (text.empty() || text == "none" || text == "transparent")
        return false;
    if (text.rfind("rgb(", 0) == 0) {
        int parts[3] { 0, 0, 0 };
        int index = 0;
        std::string body = text.substr(4);
        for (size_t i = 0; i < body.size() && index < 3;) {
            if (std::isspace(static_cast<unsigned char>(body[i])) || body[i] == ',') { ++i; continue; }
            char* end = nullptr;
            parts[index++] = static_cast<int>(std::strtod(body.c_str() + i, &end));
            i = static_cast<size_t>(end - body.c_str());
        }
        out = { static_cast<uint8_t>(std::clamp(parts[0], 0, 255)),
            static_cast<uint8_t>(std::clamp(parts[1], 0, 255)),
            static_cast<uint8_t>(std::clamp(parts[2], 0, 255)), 255 };
        return true;
    }
    if (auto color = Color::parse(text)) {
        out = *color;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Rasterisation
// ---------------------------------------------------------------------------

void blend_pixel(Image& image, int x, int y, Color color, double coverage)
{
    if (x < 0 || y < 0 || x >= image.width || y >= image.height)
        return;
    double source_alpha = (color.alpha / 255.0) * coverage;
    if (source_alpha <= 0)
        return;
    Color& destination = image.pixels[static_cast<size_t>(y) * image.width + x];
    double dest_alpha = destination.alpha / 255.0;
    double out_alpha = source_alpha + dest_alpha * (1 - source_alpha);
    if (out_alpha <= 0) {
        destination = Color { 0, 0, 0, 0 };
        return;
    }
    auto mix = [&](uint8_t s, uint8_t d) {
        return static_cast<uint8_t>(std::lround((s * source_alpha + d * dest_alpha * (1 - source_alpha)) / out_alpha));
    };
    destination.red = mix(color.red, destination.red);
    destination.green = mix(color.green, destination.green);
    destination.blue = mix(color.blue, destination.blue);
    destination.alpha = static_cast<uint8_t>(std::lround(out_alpha * 255));
}

void fill_polygon(Image& image, Polygon const& polygon, Color color, double coverage)
{
    if (polygon.size() < 3)
        return;
    double min_y = polygon[0].y;
    double max_y = polygon[0].y;
    for (auto const& p : polygon) {
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    int y0 = std::max(0, static_cast<int>(std::floor(min_y)));
    int y1 = std::min(image.height - 1, static_cast<int>(std::ceil(max_y)));

    std::vector<std::pair<double, int>> crossings;
    for (int y = y0; y <= y1; ++y) {
        double scan_y = y + 0.5;
        crossings.clear();
        for (size_t i = 0; i < polygon.size(); ++i) {
            Point const& a = polygon[i];
            Point const& b = polygon[(i + 1) % polygon.size()];
            if ((a.y <= scan_y && b.y > scan_y) || (b.y <= scan_y && a.y > scan_y)) {
                double t = (scan_y - a.y) / (b.y - a.y);
                crossings.emplace_back(a.x + t * (b.x - a.x), b.y > a.y ? 1 : -1);
            }
        }
        std::sort(crossings.begin(), crossings.end());
        int winding = 0;
        for (size_t i = 0; i + 1 < crossings.size(); ++i) {
            winding += crossings[i].second;
            if (winding == 0)
                continue;
            int xa = std::max(0, static_cast<int>(std::ceil(crossings[i].first - 0.5)));
            int xb = std::min(image.width - 1, static_cast<int>(std::floor(crossings[i + 1].first - 0.5)));
            for (int x = xa; x <= xb; ++x)
                blend_pixel(image, x, y, color, coverage);
        }
    }
}

void stroke_segment(Image& image, Point a, Point b, Color color, double width, double coverage)
{
    if (width <= 0)
        return;
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-9)
        return;
    double nx = -dy / length * (width / 2);
    double ny = dx / length * (width / 2);
    Polygon quad {
        { a.x + nx, a.y + ny }, { b.x + nx, b.y + ny },
        { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny }
    };
    fill_polygon(image, quad, color, coverage);
}

void stroke_polygon(Image& image, Polygon const& polygon, bool closed, Color color, double width, double coverage)
{
    for (size_t i = 0; i + 1 < polygon.size(); ++i)
        stroke_segment(image, polygon[i], polygon[i + 1], color, width, coverage);
    if (closed && polygon.size() >= 3)
        stroke_segment(image, polygon.back(), polygon.front(), color, width, coverage);
}

void add_ellipse(Polygon& out, double cx, double cy, double rx, double ry)
{
    constexpr int kSegments = 48;
    for (int i = 0; i < kSegments; ++i) {
        double angle = 2 * 3.14159265358979 * i / kSegments;
        out.push_back({ cx + rx * std::cos(angle), cy + ry * std::sin(angle) });
    }
}

// Parses a path 'd' attribute into sub-polygons (curves flattened).
std::vector<Polygon> parse_path(std::string const& d)
{
    std::vector<Polygon> result;
    Polygon current;
    Point cursor { 0, 0 };
    Point start { 0, 0 };
    char command = 0;

    size_t i = 0;
    auto skip_separators = [&] {
        while (i < d.size() && (std::isspace(static_cast<unsigned char>(d[i])) || d[i] == ','))
            ++i;
    };
    auto read_number = [&](bool& ok) {
        skip_separators();
        char* end = nullptr;
        double value = std::strtod(d.c_str() + i, &end);
        ok = end != d.c_str() + i;
        if (ok)
            i = static_cast<size_t>(end - d.c_str());
        return value;
    };

    auto flush = [&](bool /*closed*/) {
        if (current.size() >= 2)
            result.push_back(current);
        current.clear();
    };

    while (i < d.size()) {
        skip_separators();
        if (i >= d.size())
            break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) {
            command = d[i++];
        }
        bool relative = std::islower(static_cast<unsigned char>(command));
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        bool ok = true;
        if (upper == 'M') {
            double x = read_number(ok);
            double y = read_number(ok);
            if (!ok)
                break;
            flush(false);
            cursor = relative ? Point { cursor.x + x, cursor.y + y } : Point { x, y };
            start = cursor;
            current.push_back(cursor);
            command = relative ? 'l' : 'L';
        } else if (upper == 'L') {
            double x = read_number(ok);
            double y = read_number(ok);
            if (!ok) break;
            cursor = relative ? Point { cursor.x + x, cursor.y + y } : Point { x, y };
            current.push_back(cursor);
        } else if (upper == 'H') {
            double x = read_number(ok);
            if (!ok) break;
            cursor.x = relative ? cursor.x + x : x;
            current.push_back(cursor);
        } else if (upper == 'V') {
            double y = read_number(ok);
            if (!ok) break;
            cursor.y = relative ? cursor.y + y : y;
            current.push_back(cursor);
        } else if (upper == 'C') {
            double x1 = read_number(ok), y1 = read_number(ok);
            double x2 = read_number(ok), y2 = read_number(ok);
            double x = read_number(ok), y = read_number(ok);
            if (!ok) break;
            Point c1 { relative ? cursor.x + x1 : x1, relative ? cursor.y + y1 : y1 };
            Point c2 { relative ? cursor.x + x2 : x2, relative ? cursor.y + y2 : y2 };
            Point end { relative ? cursor.x + x : x, relative ? cursor.y + y : y };
            for (int s = 1; s <= 16; ++s) {
                double t = s / 16.0;
                double mt = 1 - t;
                double bx = mt * mt * mt * cursor.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x + t * t * t * end.x;
                double by = mt * mt * mt * cursor.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y + t * t * t * end.y;
                current.push_back({ bx, by });
            }
            cursor = end;
        } else if (upper == 'Q') {
            double x1 = read_number(ok), y1 = read_number(ok);
            double x = read_number(ok), y = read_number(ok);
            if (!ok) break;
            Point c { relative ? cursor.x + x1 : x1, relative ? cursor.y + y1 : y1 };
            Point end { relative ? cursor.x + x : x, relative ? cursor.y + y : y };
            for (int s = 1; s <= 16; ++s) {
                double t = s / 16.0;
                double mt = 1 - t;
                double bx = mt * mt * cursor.x + 2 * mt * t * c.x + t * t * end.x;
                double by = mt * mt * cursor.y + 2 * mt * t * c.y + t * t * end.y;
                current.push_back({ bx, by });
            }
            cursor = end;
        } else if (upper == 'Z') {
            flush(true);
            cursor = start;
        } else {
            break; // unsupported command
        }
    }
    flush(false);
    return result;
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

struct RenderState {
    Image& image;
    double supersample { 1 };
};

void render_node(RenderState& state, SvgNode const& node, Matrix const& transform);

void render_shape(RenderState& state, SvgNode const& node, Matrix const& transform)
{
    std::string const& tag = node.tag;
    std::vector<Polygon> polygons;
    bool closed = true;
    bool stroke_only = false;

    if (tag == "rect") {
        double x = attr_number(node, "x");
        double y = attr_number(node, "y");
        double w = attr_number(node, "width");
        double h = attr_number(node, "height");
        if (w > 0 && h > 0)
            polygons.push_back({ { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } });
    } else if (tag == "circle") {
        double cx = attr_number(node, "cx");
        double cy = attr_number(node, "cy");
        double r = attr_number(node, "r");
        Polygon circle;
        add_ellipse(circle, cx, cy, r, r);
        if (!circle.empty())
            polygons.push_back(std::move(circle));
    } else if (tag == "ellipse") {
        double cx = attr_number(node, "cx");
        double cy = attr_number(node, "cy");
        double rx = attr_number(node, "rx");
        double ry = attr_number(node, "ry");
        Polygon ellipse;
        add_ellipse(ellipse, cx, cy, rx, ry);
        if (!ellipse.empty())
            polygons.push_back(std::move(ellipse));
    } else if (tag == "line") {
        polygons.push_back({ { attr_number(node, "x1"), attr_number(node, "y1") }, { attr_number(node, "x2"), attr_number(node, "y2") } });
        stroke_only = true;
        closed = false;
    } else if (tag == "polyline" || tag == "polygon") {
        auto points = attr(node, "points");
        Polygon poly;
        if (points) {
            std::string const& text = *points;
            for (size_t i = 0; i < text.size();) {
                if (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ',') { ++i; continue; }
                char* end = nullptr;
                double x = std::strtod(text.c_str() + i, &end);
                if (end == text.c_str() + i)
                    break;
                i = static_cast<size_t>(end - text.c_str());
                while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ','))
                    ++i;
                double y = std::strtod(text.c_str() + i, &end);
                if (end == text.c_str() + i)
                    break;
                i = static_cast<size_t>(end - text.c_str());
                poly.push_back({ x, y });
            }
        }
        if (!poly.empty())
            polygons.push_back(std::move(poly));
        closed = tag == "polygon";
    } else if (tag == "path") {
        auto d = attr(node, "d");
        if (d)
            polygons = parse_path(*d);
        closed = true;
    } else {
        return;
    }

    Color fill;
    bool has_fill = false;
    if (!stroke_only) {
        if (auto value = attr(node, "fill"))
            has_fill = parse_paint(*value, fill);
        else
            has_fill = parse_paint("black", fill);
    }

    Color stroke;
    bool has_stroke = false;
    if (auto value = attr(node, "stroke"))
        has_stroke = parse_paint(*value, stroke);
    double stroke_width = attr_number(node, "stroke-width", 1.0);

    double opacity = 1.0;
    if (auto value = attr(node, "opacity"))
        opacity = std::clamp(number(*value, 1.0), 0.0, 1.0);

    double const ss = state.supersample;

    for (auto const& polygon : polygons) {
        Polygon transformed;
        transformed.reserve(polygon.size());
        for (auto const& point : polygon) {
            Point p = transform.apply(point);
            transformed.push_back({ p.x * ss, p.y * ss });
        }
        if (has_fill && closed)
            fill_polygon(state.image, transformed, fill, opacity);
        if (has_stroke)
            stroke_polygon(state.image, transformed, closed, stroke, stroke_width * ss, opacity);
    }
}

void render_node(RenderState& state, SvgNode const& node, Matrix const& transform)
{
    Matrix local = transform;
    if (auto value = attr(node, "transform"))
        local = multiply(transform, parse_transform(*value));

    if (node.tag == "rect" || node.tag == "circle" || node.tag == "ellipse" || node.tag == "line"
        || node.tag == "polyline" || node.tag == "polygon" || node.tag == "path") {
        render_shape(state, node, local);
    }

    for (auto const& child : node.children)
        render_node(state, child, local);
}

SvgNode const* find_svg_root(SvgNode const& node)
{
    if (node.tag == "svg")
        return &node;
    for (auto const& child : node.children)
        if (auto* found = find_svg_root(child))
            return found;
    return nullptr;
}

} // namespace

bool is_svg(std::vector<unsigned char> const& bytes)
{
    std::string text(bytes.begin(), bytes.end());
    auto position = text.find("<svg");
    if (position == std::string::npos)
        return false;
    // The <svg tag should appear before any body-ish markup.
    return position < 4096;
}

bool render_svg(std::vector<unsigned char> const& bytes, Image& out, int target_width, int target_height)
{
    if (!is_svg(bytes))
        return false;

    std::string text(bytes.begin(), bytes.end());

    // Build a shallow element tree from the markup.
    SvgNode root { "#document", {}, {} };
    std::vector<SvgNode*> stack { &root };
    HtmlTokenizer tokenizer(text);
    while (!tokenizer.eof()) {
        auto token = tokenizer.next_token();
        if (token.type == HtmlTokenType::StartTag || token.type == HtmlTokenType::SelfClosingTag) {
            SvgNode node;
            node.tag = to_lower(token.data);
            for (auto const& [key, value] : token.attributes)
                node.attributes[to_lower(key)] = value;
            SvgNode& placed = stack.back()->children.emplace_back(std::move(node));
            if (token.type == HtmlTokenType::StartTag && placed.tag != "svg")
                stack.push_back(&placed);
            else if (token.type == HtmlTokenType::StartTag)
                stack.push_back(&placed);
        } else if (token.type == HtmlTokenType::EndTag) {
            if (stack.size() > 1)
                stack.pop_back();
        }
    }

    SvgNode const* svg = find_svg_root(root);
    if (!svg)
        return false;

    // Viewport size.
    double width = attr_number(*svg, "width", 0);
    double height = attr_number(*svg, "height", 0);
    if (target_width > 0) width = target_width;
    if (target_height > 0) height = target_height;

    double view_x = 0, view_y = 0, view_w = 0, view_h = 0;
    bool has_view_box = false;
    if (auto value = attr(*svg, "viewbox")) {
        std::vector<double> parts;
        for (size_t i = 0; i < value->size();) {
            if (std::isspace(static_cast<unsigned char>((*value)[i])) || (*value)[i] == ',') { ++i; continue; }
            char* end = nullptr;
            double v = std::strtod(value->c_str() + i, &end);
            if (end == value->c_str() + i)
                break;
            parts.push_back(v);
            i = static_cast<size_t>(end - value->c_str());
        }
        if (parts.size() == 4) {
            view_x = parts[0];
            view_y = parts[1];
            view_w = parts[2];
            view_h = parts[3];
            has_view_box = view_w > 0 && view_h > 0;
        }
    }

    if (width <= 0)
        width = has_view_box ? view_w : 300;
    if (height <= 0)
        height = has_view_box ? view_h : 150;
    if (width <= 0 || height <= 0)
        return false;

    int pixel_width = std::max(1, static_cast<int>(std::lround(width)));
    int pixel_height = std::max(1, static_cast<int>(std::lround(height)));

    // Map user units into pixels.
    Matrix base;
    if (has_view_box) {
        double scale = std::min(pixel_width / view_w, pixel_height / view_h);
        base.a = scale;
        base.d = scale;
        base.e = -view_x * scale + (pixel_width - view_w * scale) / 2;
        base.f = -view_y * scale + (pixel_height - view_h * scale) / 2;
    }

    constexpr int kSuper = 2;
    Image canvas;
    canvas.width = pixel_width * kSuper;
    canvas.height = pixel_height * kSuper;
    canvas.pixels.assign(static_cast<size_t>(canvas.width) * canvas.height, Color { 0, 0, 0, 0 });

    // Fold the supersampling factor into the base transform.
    Matrix supersampled = base;
    supersampled.a *= kSuper;
    supersampled.d *= kSuper;
    supersampled.e *= kSuper;
    supersampled.f *= kSuper;

    RenderState state { canvas, 1.0 };
    for (auto const& child : svg->children)
        render_node(state, child, supersampled);

    // Downsample.
    out = Image {};
    out.width = pixel_width;
    out.height = pixel_height;
    out.pixels.assign(static_cast<size_t>(pixel_width) * pixel_height, Color { 0, 0, 0, 0 });
    for (int y = 0; y < pixel_height; ++y) {
        for (int x = 0; x < pixel_width; ++x) {
            int r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < kSuper; ++sy)
                for (int sx = 0; sx < kSuper; ++sx) {
                    auto const& p = canvas.pixels[static_cast<size_t>(y * kSuper + sy) * canvas.width + (x * kSuper + sx)];
                    r += p.red;
                    g += p.green;
                    b += p.blue;
                    a += p.alpha;
                }
            int n = kSuper * kSuper;
            out.pixels[static_cast<size_t>(y) * pixel_width + x] = Color {
                static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n),
                static_cast<uint8_t>(b / n), static_cast<uint8_t>(a / n)
            };
        }
    }
    return true;
}

} // namespace aetheris::rendering
