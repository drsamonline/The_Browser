#include "jpeg.hpp"

#include "color.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace aetheris::rendering {

namespace {

// Standard JPEG zig-zag ordering of the 8x8 DCT coefficients.
constexpr int kZigZag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

struct HuffTable {
    int min_code[17] {};
    int max_code[18] {};
    int val_ptr[17] {};
    std::vector<unsigned char> values;
};

struct Component {
    int id { 0 };
    int h { 1 };
    int v { 1 };
    int quant_id { 0 };
    int dc_table { 0 };
    int ac_table { 0 };
    int plane_width { 0 };
    int plane_height { 0 };
    int dc_pred { 0 };
    std::vector<unsigned char> plane;
};

struct Decoder {
    std::vector<unsigned char> const& data;
    size_t pos { 0 };

    int width { 0 };
    int height { 0 };
    int num_components { 0 };
    int restart_interval { 0 };
    int max_h { 1 };
    int max_v { 1 };
    unsigned short quant[4][64] {};
    HuffTable dc_tables[4];
    HuffTable ac_tables[4];
    Component comp[4];

    uint32_t bit_buffer { 0 };
    int bit_count { 0 };

    explicit Decoder(std::vector<unsigned char> const& bytes) : data(bytes) {}

    int read_byte()
    {
        if (pos >= data.size())
            return -1;
        unsigned char b = data[pos];
        if (b == 0xFF) {
            if (pos + 1 >= data.size())
                return -1;
            unsigned char next = data[pos + 1];
            if (next == 0x00) { pos += 2; return 0xFF; }
            return -1; // real marker: leave pos pointing at 0xFF
        }
        ++pos;
        return b;
    }

    int read_bit()
    {
        if (bit_count == 0) {
            int b = read_byte();
            if (b < 0)
                return -1;
            bit_buffer = static_cast<uint32_t>(b);
            bit_count = 8;
        }
        --bit_count;
        return static_cast<int>((bit_buffer >> bit_count) & 1u);
    }

    int read_bits(int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            int b = read_bit();
            if (b < 0)
                return -1;
            value = (value << 1) | b;
        }
        return value;
    }

    int decode_huffman(HuffTable const& table)
    {
        int code = 0;
        for (int length = 1; length <= 16; ++length) {
            int b = read_bit();
            if (b < 0)
                return -1;
            code = (code << 1) | b;
            if (table.max_code[length] >= 0 && code <= table.max_code[length]) {
                int index = table.val_ptr[length] + code - table.min_code[length];
                if (index >= 0 && index < static_cast<int>(table.values.size()))
                    return table.values[index];
                return -1;
            }
        }
        return -1;
    }

    static void build_huffman(HuffTable& table, int const* counts, unsigned char const* values, int n)
    {
        table.values.assign(values, values + n);
        int code = 0;
        int k = 0;
        for (int length = 1; length <= 16; ++length) {
            if (counts[length] == 0) {
                table.max_code[length] = -1;
                table.min_code[length] = 0;
                table.val_ptr[length] = 0;
            } else {
                table.val_ptr[length] = k;
                table.min_code[length] = code;
                code += counts[length];
                k += counts[length];
                table.max_code[length] = code - 1;
            }
            code <<= 1;
        }
    }

    static int extend(int value, int bits)
    {
        if (bits == 0)
            return 0;
        int threshold = 1 << (bits - 1);
        if (value < threshold)
            value += (-1 << bits) + 1;
        return value;
    }

    static void idct(float* block)
    {
        static float cosine[8][8];
        static bool ready = false;
        if (!ready) {
            for (int x = 0; x < 8; ++x)
                for (int u = 0; u < 8; ++u) {
                    float scale = (u == 0) ? 0.70710678f : 1.0f;
                    cosine[x][u] = scale * std::cos((2 * x + 1) * u * 3.14159265358979f / 16.0f);
                }
            ready = true;
        }
        float temp[64];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                float sum = 0;
                for (int u = 0; u < 8; ++u)
                    sum += cosine[x][u] * block[y * 8 + u];
                temp[y * 8 + x] = sum * 0.5f;
            }
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y) {
                float sum = 0;
                for (int v = 0; v < 8; ++v)
                    sum += cosine[y][v] * temp[v * 8 + x];
                block[y * 8 + x] = sum * 0.5f;
            }
    }

    bool decode_block(Component& component, int* coefficients)
    {
        for (int i = 0; i < 64; ++i)
            coefficients[i] = 0;

        int dc_bits = decode_huffman(dc_tables[component.dc_table]);
        if (dc_bits < 0)
            return false;
        int dc_value = dc_bits == 0 ? 0 : read_bits(dc_bits);
        if (dc_bits != 0 && dc_value < 0)
            return false;
        component.dc_pred += extend(dc_value, dc_bits);
        coefficients[0] = component.dc_pred;

        for (int k = 1; k < 64;) {
            int rs = decode_huffman(ac_tables[component.ac_table]);
            if (rs < 0)
                return false;
            int run = rs >> 4;
            int size = rs & 15;
            if (size == 0) {
                if (run == 15) { k += 16; continue; }
                break;
            }
            k += run;
            if (k > 63)
                break;
            int value = read_bits(size);
            if (value < 0)
                return false;
            coefficients[kZigZag[k]] = extend(value, size);
            ++k;
        }
        return true;
    }

    bool decode_scan()
    {
        int mcu_width = 8 * max_h;
        int mcu_height = 8 * max_v;
        int mcus_x = (width + mcu_width - 1) / mcu_width;
        int mcus_y = (height + mcu_height - 1) / mcu_height;

        int mcu_index = 0;
        for (int my = 0; my < mcus_y; ++my) {
            for (int mx = 0; mx < mcus_x; ++mx) {
                if (restart_interval && mcu_index > 0 && mcu_index % restart_interval == 0) {
                    bit_count = 0;
                    if (pos + 1 < data.size() && data[pos] == 0xFF && data[pos + 1] >= 0xD0 && data[pos + 1] <= 0xD7)
                        pos += 2;
                    for (int c = 0; c < num_components; ++c)
                        comp[c].dc_pred = 0;
                }
                ++mcu_index;

                for (int c = 0; c < num_components; ++c) {
                    Component& component = comp[c];
                    for (int by = 0; by < component.v; ++by) {
                        for (int bx = 0; bx < component.h; ++bx) {
                            int coefficients[64];
                            if (!decode_block(component, coefficients))
                                return false;
                            float block[64];
                            for (int i = 0; i < 64; ++i)
                                block[i] = static_cast<float>(coefficients[i]) * static_cast<float>(quant[component.quant_id][i]);
                            idct(block);
                            int origin_x = (mx * component.h + bx) * 8;
                            int origin_y = (my * component.v + by) * 8;
                            for (int y = 0; y < 8; ++y) {
                                int py = origin_y + y;
                                if (py >= component.plane_height)
                                    break;
                                for (int x = 0; x < 8; ++x) {
                                    int px = origin_x + x;
                                    if (px >= component.plane_width)
                                        break;
                                    int sample = static_cast<int>(std::lround(block[y * 8 + x])) + 128;
                                    component.plane[static_cast<size_t>(py) * component.plane_width + px]
                                        = static_cast<unsigned char>(sample < 0 ? 0 : (sample > 255 ? 255 : sample));
                                }
                            }
                        }
                    }
                }
            }
        }
        return true;
    }

    unsigned char sample(Component const& component, int x, int y) const
    {
        int sx = x * component.h / max_h;
        int sy = y * component.v / max_v;
        if (sx >= component.plane_width) sx = component.plane_width - 1;
        if (sy >= component.plane_height) sy = component.plane_height - 1;
        return component.plane[static_cast<size_t>(sy) * component.plane_width + sx];
    }
};

} // namespace

bool is_jpeg(std::vector<unsigned char> const& bytes)
{
    return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

bool decode_jpeg(std::vector<unsigned char> const& bytes, Image& out)
{
    if (!is_jpeg(bytes))
        return false;

    Decoder decoder(bytes);
    decoder.pos = 2;

    bool saw_scan = false;

    while (decoder.pos + 1 < bytes.size()) {
        if (bytes[decoder.pos] != 0xFF) { ++decoder.pos; continue; }
        unsigned char marker = bytes[decoder.pos + 1];
        if (marker == 0xFF) { ++decoder.pos; continue; }
        decoder.pos += 2;

        if (marker == 0xD9) // EOI
            break;
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7))
            continue;
        if (decoder.pos + 2 > bytes.size())
            return false;
        int length = (bytes[decoder.pos] << 8) | bytes[decoder.pos + 1];
        if (length < 2 || decoder.pos + length > bytes.size())
            return false;
        unsigned char const* segment = &bytes[decoder.pos + 2];
        int segment_length = length - 2;

        if (marker == 0xDB) { // DQT
            int offset = 0;
            while (offset < segment_length) {
                int pq = segment[offset] >> 4;
                int tq = segment[offset] & 15;
                ++offset;
                if (tq > 3)
                    return false;
                for (int i = 0; i < 64; ++i) {
                    int value;
                    if (pq == 0) { value = segment[offset]; offset += 1; }
                    else { value = (segment[offset] << 8) | segment[offset + 1]; offset += 2; }
                    decoder.quant[tq][kZigZag[i]] = static_cast<unsigned short>(value);
                }
            }
        } else if (marker == 0xC0 || marker == 0xC1) { // SOF0/SOF1 baseline
            if (segment_length < 6)
                return false;
            int precision = segment[0];
            if (precision != 8)
                return false;
            decoder.height = (segment[1] << 8) | segment[2];
            decoder.width = (segment[3] << 8) | segment[4];
            decoder.num_components = segment[5];
            if (decoder.width <= 0 || decoder.height <= 0 || decoder.num_components < 1 || decoder.num_components > 4)
                return false;
            for (int c = 0; c < decoder.num_components; ++c) {
                int base = 6 + c * 3;
                if (base + 2 >= segment_length)
                    return false;
                decoder.comp[c].id = segment[base];
                decoder.comp[c].h = segment[base + 1] >> 4;
                decoder.comp[c].v = segment[base + 1] & 15;
                decoder.comp[c].quant_id = segment[base + 2];
                if (decoder.comp[c].h < 1 || decoder.comp[c].v < 1 || decoder.comp[c].h > 4 || decoder.comp[c].v > 4)
                    return false;
                if (decoder.comp[c].h > decoder.max_h) decoder.max_h = decoder.comp[c].h;
                if (decoder.comp[c].v > decoder.max_v) decoder.max_v = decoder.comp[c].v;
            }
        } else if (marker == 0xC4) { // DHT
            int offset = 0;
            while (offset < segment_length) {
                int tc = segment[offset] >> 4;
                int th = segment[offset] & 15;
                ++offset;
                if (th > 3 || offset + 16 > segment_length)
                    return false;
                int counts[17] {};
                int total = 0;
                for (int i = 1; i <= 16; ++i) {
                    counts[i] = segment[offset + i - 1];
                    total += counts[i];
                }
                offset += 16;
                if (offset + total > segment_length)
                    return false;
                if (tc == 0)
                    Decoder::build_huffman(decoder.dc_tables[th], counts, &segment[offset], total);
                else
                    Decoder::build_huffman(decoder.ac_tables[th], counts, &segment[offset], total);
                offset += total;
            }
        } else if (marker == 0xDD) { // DRI
            if (segment_length < 2)
                return false;
            decoder.restart_interval = (segment[0] << 8) | segment[1];
        } else if (marker == 0xDA) { // SOS
            if (segment_length < 1)
                return false;
            int scan_components = segment[0];
            for (int i = 0; i < scan_components; ++i) {
                int base = 1 + i * 2;
                if (base + 1 >= segment_length)
                    return false;
                int id = segment[base];
                int tables = segment[base + 1];
                for (int c = 0; c < decoder.num_components; ++c) {
                    if (decoder.comp[c].id == id) {
                        decoder.comp[c].dc_table = tables >> 4;
                        decoder.comp[c].ac_table = tables & 15;
                    }
                }
            }

            // Allocate component planes.
            int mcu_width = 8 * decoder.max_h;
            int mcu_height = 8 * decoder.max_v;
            int mcus_x = (decoder.width + mcu_width - 1) / mcu_width;
            int mcus_y = (decoder.height + mcu_height - 1) / mcu_height;
            for (int c = 0; c < decoder.num_components; ++c) {
                decoder.comp[c].plane_width = mcus_x * decoder.comp[c].h * 8;
                decoder.comp[c].plane_height = mcus_y * decoder.comp[c].v * 8;
                decoder.comp[c].plane.assign(
                    static_cast<size_t>(decoder.comp[c].plane_width) * decoder.comp[c].plane_height, 0);
                decoder.comp[c].dc_pred = 0;
            }

            decoder.pos += length; // move past the scan header into the entropy data
            if (!decoder.decode_scan())
                return false;
            saw_scan = true;
            continue;
        }

        decoder.pos += length;
    }

    if (!saw_scan)
        return false;

    out = Image {};
    out.width = decoder.width;
    out.height = decoder.height;
    out.pixels.resize(static_cast<size_t>(decoder.width) * decoder.height);

    bool grayscale = decoder.num_components == 1;
    for (int y = 0; y < decoder.height; ++y) {
        for (int x = 0; x < decoder.width; ++x) {
            Color color {};
            if (grayscale) {
                auto grey = decoder.sample(decoder.comp[0], x, y);
                color = { grey, grey, grey, 255 };
            } else {
                int luminance = decoder.sample(decoder.comp[0], x, y);
                int cb = decoder.sample(decoder.comp[1], x, y) - 128;
                int cr = decoder.sample(decoder.comp[2], x, y) - 128;
                int r = static_cast<int>(std::lround(luminance + 1.402f * cr));
                int g = static_cast<int>(std::lround(luminance - 0.344136f * cb - 0.714136f * cr));
                int b = static_cast<int>(std::lround(luminance + 1.772f * cb));
                color = {
                    static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r)),
                    static_cast<uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g)),
                    static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b)),
                    255
                };
            }
            out.pixels[static_cast<size_t>(y) * decoder.width + x] = color;
        }
    }
    return true;
}

} // namespace aetheris::rendering
