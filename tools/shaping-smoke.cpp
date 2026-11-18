#include <hb.h>
#include <hb-ot.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
static unsigned shape(hb_font_t* font, char const* text, hb_feature_t const* feats, unsigned nfeat, unsigned* first_gid)
{
    hb_buffer_t* b = hb_buffer_create();
    hb_buffer_add_utf8(b, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(b);
    hb_shape(font, b, feats, nfeat);
    unsigned count = hb_buffer_get_length(b);
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(b, nullptr);
    if (first_gid && count) *first_gid = info[0].codepoint;
    hb_buffer_destroy(b);
    return count;
}
int main(int argc, char** argv)
{
    std::ifstream in(argv[1], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    hb_blob_t* blob = hb_blob_create(bytes.data(), (unsigned)bytes.size(), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_font_t* font = hb_font_create(hb_face_create(blob, 0));
    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, 64 * 16, 64 * 16);

    unsigned gid = 0;
    hb_feature_t liga_on;  hb_feature_from_string("liga=1", -1, &liga_on);
    hb_feature_t liga_off; hb_feature_from_string("liga=0", -1, &liga_off);

    std::printf("fi   liga on  -> %u glyphs (first gid %u)\n", shape(font, "fi", &liga_on, 1, &gid), gid);
    std::printf("fi   liga off -> %u glyphs\n", shape(font, "fi", &liga_off, 1, nullptr));
    std::printf("fx            -> %u glyphs\n", shape(font, "fx", nullptr, 0, nullptr));
    std::printf("ab            -> %u glyphs\n", shape(font, "ab", nullptr, 0, nullptr));
    std::printf("Arabic 4 cps  -> %u glyphs\n", shape(font, "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", nullptr, 0, nullptr));
    return 0;
}
