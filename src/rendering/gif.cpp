#include "gif.hpp"

#include "color.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace aetheris::rendering {

namespace {

uint16_t read_u16_le(unsigned char const* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// GIF LZW decoder. Produces palette indices.
bool lzw_decode(std::vector<unsigned char> const& data, int min_code_size, std::vector<unsigned char>& out)
{
    if (min_code_size < 2 || min_code_size > 8)
        return false;

    int clear_code = 1 << min_code_size;
    int end_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = end_code + 1;

    std::vector<std::vector<unsigned char>> dictionary(4096);
    for (int i = 0; i < clear_code; ++i)
        dictionary[i] = { static_cast<unsigned char>(i) };

    size_t bit_position = 0;
    auto read_code = [&](int size) -> int {
        int code = 0;
        for (int i = 0; i < size; ++i) {
            size_t byte_index = bit_position >> 3;
            if (byte_index >= data.size())
                return -1;
            int bit = (data[byte_index] >> (bit_position & 7)) & 1;
            code |= bit << i;
            ++bit_position;
        }
        return code;
    };

    std::vector<unsigned char> previous;
    for (;;) {
        int code = read_code(code_size);
        if (code < 0)
            break;
        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = end_code + 1;
            previous.clear();
            continue;
        }
        if (code == end_code)
            break;

        std::vector<unsigned char> entry;
        if (code < next_code && !dictionary[code].empty()) {
            entry = dictionary[code];
        } else if (!previous.empty()) {
            entry = previous;
            entry.push_back(previous.front());
        } else {
            break;
        }

        out.insert(out.end(), entry.begin(), entry.end());

        if (!previous.empty() && next_code < 4096) {
            std::vector<unsigned char> created = previous;
            created.push_back(entry.front());
            dictionary[next_code++] = std::move(created);
            if (next_code == (1 << code_size) && code_size < 12)
                ++code_size;
        }
        previous = std::move(entry);
    }
    return true;
}

struct ColorTable {
    std::vector<Color> entries;
};

ColorTable read_color_table(unsigned char const* data, int size)
{
    ColorTable table;
    table.entries.resize(size);
    for (int i = 0; i < size; ++i)
        table.entries[i] = { data[i * 3], data[i * 3 + 1], data[i * 3 + 2], 255 };
    return table;
}

// Reads a chain of data sub-blocks (length byte + payload, terminated by 0).
bool read_sub_blocks(std::vector<unsigned char> const& bytes, size_t& pos, std::vector<unsigned char>& out)
{
    while (pos < bytes.size()) {
        int length = bytes[pos++];
        if (length == 0)
            return true;
        if (pos + length > bytes.size())
            return false;
        out.insert(out.end(), bytes.begin() + pos, bytes.begin() + pos + length);
        pos += length;
    }
    return false;
}

} // namespace

bool is_gif(std::vector<unsigned char> const& bytes)
{
    if (bytes.size() < 6)
        return false;
    return std::memcmp(bytes.data(), "GIF87a", 6) == 0
        || std::memcmp(bytes.data(), "GIF89a", 6) == 0;
}

bool decode_gif(std::vector<unsigned char> const& bytes, Image& out)
{
    if (!is_gif(bytes))
        return false;

    size_t pos = 6;
    if (pos + 7 > bytes.size())
        return false;
    int screen_width = read_u16_le(&bytes[pos]);
    int screen_height = read_u16_le(&bytes[pos + 2]);
    unsigned char packed = bytes[pos + 4];
    pos += 7;
    if (screen_width <= 0 || screen_height <= 0)
        return false;

    ColorTable global_table;
    if (packed & 0x80) {
        int size = 1 << ((packed & 0x07) + 1);
        if (pos + static_cast<size_t>(size) * 3 > bytes.size())
            return false;
        global_table = read_color_table(&bytes[pos], size);
        pos += static_cast<size_t>(size) * 3;
    }

    int transparent_index = -1;

    while (pos < bytes.size()) {
        unsigned char block = bytes[pos++];
        if (block == 0x3B) // trailer
            break;

        if (block == 0x21) { // extension
            if (pos >= bytes.size())
                return false;
            unsigned char label = bytes[pos++];
            if (label == 0xF9) { // graphic control extension
                if (pos + 6 > bytes.size())
                    return false;
                unsigned char flags = bytes[pos + 1];
                if (flags & 0x01)
                    transparent_index = bytes[pos + 4];
                // skip the sub-block (6 bytes: size + 4 payload + terminator)
                size_t skip = bytes[pos];
                pos += 1 + skip;
                if (pos < bytes.size() && bytes[pos] == 0)
                    ++pos;
            } else {
                std::vector<unsigned char> ignored;
                if (!read_sub_blocks(bytes, pos, ignored))
                    return false;
            }
            continue;
        }

        if (block == 0x2C) { // image descriptor
            if (pos + 9 > bytes.size())
                return false;
            int frame_left = read_u16_le(&bytes[pos]);
            int frame_top = read_u16_le(&bytes[pos + 2]);
            int frame_width = read_u16_le(&bytes[pos + 4]);
            int frame_height = read_u16_le(&bytes[pos + 6]);
            unsigned char frame_packed = bytes[pos + 8];
            pos += 9;
            if (frame_width <= 0 || frame_height <= 0)
                return false;

            bool interlaced = (frame_packed & 0x40) != 0;
            ColorTable local_table;
            if (frame_packed & 0x80) {
                int size = 1 << ((frame_packed & 0x07) + 1);
                if (pos + static_cast<size_t>(size) * 3 > bytes.size())
                    return false;
                local_table = read_color_table(&bytes[pos], size);
                pos += static_cast<size_t>(size) * 3;
            }
            ColorTable const& table = local_table.entries.empty() ? global_table : local_table;

            if (pos >= bytes.size())
                return false;
            int min_code_size = bytes[pos++];

            std::vector<unsigned char> compressed;
            if (!read_sub_blocks(bytes, pos, compressed))
                return false;

            std::vector<unsigned char> indices;
            if (!lzw_decode(compressed, min_code_size, indices))
                return false;

            // Row order (interlaced GIFs store rows in four passes).
            std::vector<int> row_order;
            row_order.reserve(frame_height);
            if (interlaced) {
                for (int pass = 0; pass < 4; ++pass) {
                    static const int starts[4] = { 0, 4, 2, 1 };
                    static const int steps[4] = { 8, 8, 4, 2 };
                    for (int y = starts[pass]; y < frame_height; y += steps[pass])
                        row_order.push_back(y);
                }
            } else {
                for (int y = 0; y < frame_height; ++y)
                    row_order.push_back(y);
            }

            out = Image {};
            out.width = screen_width;
            out.height = screen_height;
            out.pixels.assign(static_cast<size_t>(screen_width) * screen_height, Color { 0, 0, 0, 0 });

            size_t index = 0;
            for (int row = 0; row < frame_height; ++row) {
                int y = row_order[row];
                for (int x = 0; x < frame_width; ++x) {
                    if (index >= indices.size())
                        break;
                    auto palette_index = indices[index++];
                    int px = frame_left + x;
                    int py = frame_top + y;
                    if (px < 0 || py < 0 || px >= screen_width || py >= screen_height)
                        continue;
                    if (static_cast<int>(palette_index) == transparent_index)
                        continue;
                    if (palette_index < table.entries.size())
                        out.pixels[static_cast<size_t>(py) * screen_width + px] = table.entries[palette_index];
                }
            }
            return true; // first frame only
        }

        // Unknown block: stop to avoid misparsing.
        break;
    }

    return false;
}

} // namespace aetheris::rendering
