#include "png.hpp"

#include "color.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace aetheris::rendering {

namespace {

// ---------------------------------------------------------------------------
// DEFLATE (RFC 1951) — compact inflate adapted from Mark Adler's public-domain
// "puff" reference decoder. Bit order, Huffman construction and length/distance
// tables follow the RFC.
// ---------------------------------------------------------------------------

constexpr int kMaxBits = 15;
constexpr int kMaxLens = 288;
constexpr int kMaxCodes = 286;
constexpr int kMaxDists = 30;

struct Huffman {
    uint16_t count[kMaxBits + 1] {};
    uint16_t symbol[kMaxLens] {};
};

struct Inflater {
    unsigned char const* in { nullptr };
    size_t in_length { 0 };
    size_t in_cursor { 0 };
    unsigned long bit_buffer { 0 };
    int bit_count { 0 };
    std::vector<unsigned char>* out { nullptr };
    bool overflow { false };
};

int bits(Inflater& state, int need)
{
    unsigned long value = state.bit_buffer;
    while (state.bit_count < need) {
        if (state.in_cursor == state.in_length)
            return -1;
        value |= static_cast<unsigned long>(state.in[state.in_cursor++]) << state.bit_count;
        state.bit_count += 8;
    }
    state.bit_buffer = value >> need;
    state.bit_count -= need;
    return static_cast<int>(value & ((1UL << need) - 1));
}

int decode_symbol(Inflater& state, Huffman const& table)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length <= kMaxBits; ++length) {
        int bit = bits(state, 1);
        if (bit < 0)
            return bit;
        code |= bit;
        int count = table.count[length];
        if (code - count < first)
            return table.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

int construct(Huffman& table, uint16_t const* lengths, int n)
{
    for (int i = 0; i <= kMaxBits; ++i)
        table.count[i] = 0;
    for (int symbol = 0; symbol < n; ++symbol)
        table.count[lengths[symbol]]++;

    if (table.count[0] == n)
        return 0;

    int left = 1;
    for (int length = 1; length <= kMaxBits; ++length) {
        left <<= 1;
        left -= table.count[length];
        if (left < 0)
            return left;
    }

    uint16_t offsets[kMaxBits + 1] {};
    offsets[1] = 0;
    for (int length = 1; length < kMaxBits; ++length)
        offsets[length + 1] = static_cast<uint16_t>(offsets[length] + table.count[length]);
    for (int symbol = 0; symbol < n; ++symbol)
        if (lengths[symbol] != 0)
            table.symbol[offsets[lengths[symbol]]++] = static_cast<uint16_t>(symbol);
    return left;
}

int inflate_block(Inflater& state, Huffman const& length_table, Huffman const& distance_table)
{
    static const uint16_t length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const uint16_t length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const uint16_t distance_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const uint16_t distance_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    int symbol = 0;
    do {
        symbol = decode_symbol(state, length_table);
        if (symbol < 0)
            return symbol;
        if (symbol < 256) {
            state.out->push_back(static_cast<unsigned char>(symbol));
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29)
                return -10;
            int extra = bits(state, length_extra[symbol]);
            if (extra < 0)
                return extra;
            int length = length_base[symbol] + extra;

            int distance_symbol = decode_symbol(state, distance_table);
            if (distance_symbol < 0)
                return distance_symbol;
            if (distance_symbol >= 30)
                return -11;
            int distance_extra_bits = bits(state, distance_extra[distance_symbol]);
            if (distance_extra_bits < 0)
                return distance_extra_bits;
            int distance = distance_base[distance_symbol] + distance_extra_bits;

            if (distance > static_cast<int>(state.out->size()))
                return -12;
            for (int i = 0; i < length; ++i) {
                auto byte = (*state.out)[state.out->size() - distance];
                state.out->push_back(byte);
            }
        }
    } while (symbol != 256);
    return 0;
}

int inflate_codes(Inflater& state, unsigned char const* data, size_t length)
{
    // Fixed literal/length and distance tables (RFC 1951 §3.2.6).
    static const uint16_t fixed_lengths[kMaxLens] = {
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8
    };
    static const uint16_t fixed_distances[kMaxDists] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };

    Huffman length_table;
    Huffman distance_table;

    state.in = data;
    state.in_length = length;
    state.in_cursor = 0;
    state.bit_buffer = 0;
    state.bit_count = 0;

    int last = 0;
    int result = 0;
    do {
        last = bits(state, 1);
        if (last < 0)
            return last;
        int type = bits(state, 2);
        if (type < 0)
            return type;

        if (type == 0) {
            // Stored block.
            state.bit_buffer = 0;
            state.bit_count = 0;
            if (state.in_cursor + 4 > state.in_length)
                return -5;
            unsigned len = state.in[state.in_cursor] | (static_cast<unsigned>(state.in[state.in_cursor + 1]) << 8);
            unsigned nlen = state.in[state.in_cursor + 2] | (static_cast<unsigned>(state.in[state.in_cursor + 3]) << 8);
            state.in_cursor += 4;
            if ((len ^ 0xFFFFu) != nlen)
                return -6;
            if (state.in_cursor + len > state.in_length)
                return -7;
            for (unsigned i = 0; i < len; ++i)
                state.out->push_back(state.in[state.in_cursor++]);
        } else if (type == 1) {
            // The fixed distance code is intentionally incomplete (30 of the 32
            // length-5 codes), so construct's "unused codes" result is ignored
            // here, exactly as the reference decoder does.
            construct(length_table, fixed_lengths, kMaxLens);
            construct(distance_table, fixed_distances, kMaxDists);
            result = inflate_block(state, length_table, distance_table);
            if (result != 0)
                return result;
        } else if (type == 2) {
            static const uint16_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            uint16_t code_lengths[19] {};
            int hlit = bits(state, 5);
            int hdist = bits(state, 5);
            int hclen = bits(state, 4);
            if (hlit < 0 || hdist < 0 || hclen < 0)
                return -1;
            hlit += 257;
            hdist += 1;
            hclen += 4;
            if (hlit > kMaxLens || hdist > kMaxDists)
                return -2;
            for (int i = 0; i < hclen; ++i) {
                int value = bits(state, 3);
                if (value < 0)
                    return value;
                code_lengths[order[i]] = static_cast<uint16_t>(value);
            }
            Huffman code_table;
            result = construct(code_table, code_lengths, 19);
            if (result != 0)
                return result;

            uint16_t lengths[kMaxLens + kMaxDists] {};
            int index = 0;
            while (index < hlit + hdist) {
                int symbol = decode_symbol(state, code_table);
                if (symbol < 0)
                    return symbol;
                if (symbol < 16) {
                    lengths[index++] = static_cast<uint16_t>(symbol);
                } else {
                    int repeat = 0;
                    uint16_t value = 0;
                    if (symbol == 16) {
                        if (index == 0)
                            return -3;
                        value = lengths[index - 1];
                        repeat = bits(state, 2);
                        if (repeat < 0)
                            return repeat;
                        repeat += 3;
                    } else if (symbol == 17) {
                        repeat = bits(state, 3);
                        if (repeat < 0)
                            return repeat;
                        repeat += 3;
                    } else {
                        repeat = bits(state, 7);
                        if (repeat < 0)
                            return repeat;
                        repeat += 11;
                    }
                    if (index + repeat > hlit + hdist)
                        return -4;
                    while (repeat-- > 0)
                        lengths[index++] = value;
                }
            }

            result = construct(length_table, lengths, hlit);
            if (result != 0)
                return result;
            result = construct(distance_table, lengths + hlit, hdist);
            if (result != 0)
                return result;
            result = inflate_block(state, length_table, distance_table);
            if (result != 0)
                return result;
        } else {
            return -8;
        }
    } while (!last);
    return 0;
}

// ---------------------------------------------------------------------------
// PNG container
// ---------------------------------------------------------------------------

uint32_t read_u32(unsigned char const* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
        | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

[[maybe_unused]] uint32_t crc32(unsigned char const* data, size_t length)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

} // namespace

bool is_png(std::vector<unsigned char> const& bytes)
{
    static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return bytes.size() >= 8 && std::memcmp(bytes.data(), signature, 8) == 0;
}

bool decode_png(std::vector<unsigned char> const& bytes, Image& out)
{
    if (!is_png(bytes))
        return false;

    int width = 0;
    int height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int interlace = 0;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> palette_alpha;
    std::vector<unsigned char> idat;

    size_t pos = 8;
    while (pos + 8 <= bytes.size()) {
        uint32_t length = read_u32(&bytes[pos]);
        unsigned char const* type = &bytes[pos + 4];
        size_t data_start = pos + 8;
        if (data_start + length + 4 > bytes.size())
            return false;
        unsigned char const* data = &bytes[data_start];

        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            width = static_cast<int>(read_u32(data));
            height = static_cast<int>(read_u32(data + 4));
            bit_depth = data[8];
            color_type = data[9];
            interlace = data[12];
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(data, data + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            palette_alpha.assign(data, data + length);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos = data_start + length + 4;
    }

    if (width <= 0 || height <= 0)
        return false;
    // Interlaced images are handled for byte-aligned pixels (8/16-bit).
    if (interlace != 0 && bit_depth < 8)
        return false;
    if (bit_depth != 8 && bit_depth != 16 && !(color_type == 3 && (bit_depth == 1 || bit_depth == 2 || bit_depth == 4)))
        return false;

    int channels = 0;
    switch (color_type) {
    case 0: channels = 1; break; // grey
    case 2: channels = 3; break; // RGB
    case 3: channels = 1; break; // palette index
    case 4: channels = 2; break; // grey + alpha
    case 6: channels = 4; break; // RGBA
    default: return false;
    }

    std::vector<unsigned char> raw;
    if (!idat.empty()) {
        // Strip the 2-byte zlib header; the raw DEFLATE stream follows.
        if (idat.size() < 2)
            return false;
        Inflater state;
        state.out = &raw;
        if (inflate_codes(state, idat.data() + 2, idat.size() - 2) != 0)
            return false;
    }

    int bits_per_pixel = channels * bit_depth;
    int filter_bpp = (bits_per_pixel + 7) / 8;
    int full_stride = (width * bits_per_pixel + 7) / 8;

    std::vector<unsigned char> image_data(static_cast<size_t>(height) * full_stride, 0);
    size_t raw_pos = 0;

    // Unfilter one pass (non-interlaced is a single full-size pass) and scatter
    // its pixels into the full-resolution image. Adam7 passes interleave rows
    // and columns; the plain case is x_step = y_step = 1.
    auto unfilter_pass = [&](int pass_width, int pass_height, int x0, int y0, int x_step, int y_step) -> bool {
        if (pass_width <= 0 || pass_height <= 0)
            return true;
        int pass_stride = (pass_width * bits_per_pixel + 7) / 8;
        if (raw_pos + static_cast<size_t>(pass_height) * (pass_stride + 1) > raw.size())
            return false;
        std::vector<unsigned char> line(static_cast<size_t>(pass_stride), 0);
        std::vector<unsigned char> previous(static_cast<size_t>(pass_stride), 0);
        int bytes_per_pixel = bits_per_pixel / 8;
        for (int py = 0; py < pass_height; ++py) {
            int filter = raw[raw_pos++];
            unsigned char const* src = &raw[raw_pos];
            raw_pos += pass_stride;
            for (int i = 0; i < pass_stride; ++i) {
                int value = src[i];
                int left = i >= filter_bpp ? line[i - filter_bpp] : 0;
                int up = previous[i];
                int up_left = i >= filter_bpp ? previous[i - filter_bpp] : 0;
                switch (filter) {
                case 0: break;
                case 1: value += left; break;
                case 2: value += up; break;
                case 3: value += (left + up) / 2; break;
                case 4: value += paeth(left, up, up_left); break;
                default: return false;
                }
                line[i] = static_cast<unsigned char>(value & 0xFF);
            }
            unsigned char* target = &image_data[static_cast<size_t>(y0 + py * y_step) * full_stride];
            if (x_step == 1) {
                std::memcpy(target, line.data(), static_cast<size_t>(pass_stride));
            } else if (bytes_per_pixel > 0) {
                for (int px = 0; px < pass_width; ++px)
                    std::memcpy(target + static_cast<size_t>(x0 + px * x_step) * bytes_per_pixel,
                        line.data() + static_cast<size_t>(px) * bytes_per_pixel, static_cast<size_t>(bytes_per_pixel));
            }
            previous = line;
        }
        return true;
    };

    if (interlace == 1) {
        static const int x_starts[7] = { 0, 4, 0, 2, 0, 1, 0 };
        static const int y_starts[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int x_steps[7] = { 8, 8, 4, 4, 2, 2, 1 };
        static const int y_steps[7] = { 8, 8, 8, 4, 4, 2, 2 };
        for (int pass = 0; pass < 7; ++pass) {
            int pass_width = (width - x_starts[pass] + x_steps[pass] - 1) / x_steps[pass];
            int pass_height = (height - y_starts[pass] + y_steps[pass] - 1) / y_steps[pass];
            if (!unfilter_pass(pass_width, pass_height, x_starts[pass], y_starts[pass], x_steps[pass], y_steps[pass]))
                return false;
        }
    } else if (!unfilter_pass(width, height, 0, 0, 1, 1)) {
        return false;
    }

    out = Image {};
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height);

    auto sample = [&](int x, int y, int channel) -> unsigned char {
        size_t bit_index = static_cast<size_t>(y) * full_stride * 8 + static_cast<size_t>(x) * bits_per_pixel + static_cast<size_t>(channel) * bit_depth;
        size_t byte_index = bit_index / 8;
        if (bit_depth == 8) {
            return image_data[byte_index];
        }
        if (bit_depth == 16) {
            return image_data[byte_index]; // high byte
        }
        // Sub-byte depth (palette 1/2/4): take the relevant bits.
        int bit_offset = static_cast<int>(bit_index % 8);
        int shift = 8 - bit_depth - bit_offset;
        return static_cast<unsigned char>((image_data[byte_index] >> shift) & ((1 << bit_depth) - 1));
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color color {};
            if (color_type == 0) {
                auto grey = sample(x, y, 0);
                color = { grey, grey, grey, 255 };
            } else if (color_type == 2) {
                color = { sample(x, y, 0), sample(x, y, 1), sample(x, y, 2), 255 };
            } else if (color_type == 3) {
                auto index = sample(x, y, 0);
                size_t base = static_cast<size_t>(index) * 3;
                if (base + 2 >= palette.size())
                    return false;
                uint8_t alpha = index < palette_alpha.size() ? palette_alpha[index] : 255;
                color = { palette[base], palette[base + 1], palette[base + 2], alpha };
            } else if (color_type == 4) {
                auto grey = sample(x, y, 0);
                color = { grey, grey, grey, sample(x, y, 1) };
            } else {
                color = { sample(x, y, 0), sample(x, y, 1), sample(x, y, 2), sample(x, y, 3) };
            }
            out.pixels[static_cast<size_t>(y) * width + x] = color;
        }
    }

    return true;
}

} // namespace aetheris::rendering
