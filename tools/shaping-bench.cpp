// Measure what one shaped run costs, and where the time goes, so a claim about shaping cost is a
// number instead of an impression. Row 2d4 exists because the only figure we had (1.043 ms per
// 24-character run) was taken at -O0 on a build that did not yet have the face cache.
//
// What it measures, and what each number decides:
//
//   shape_run, cached     the entry point Font::measure calls, repeated on one string: the hit path,
//                         which is what layout pays for a paragraph it has already measured
//   shape_run, cold       the same call on text the cache has never seen: the miss path, the ceiling
//   hb_shape floor        HarfBuzz alone with the face and font already built: what shaping itself costs
//   hb_font_create        no longer per call, priced so the next session knows what it would buy
//   font hash             no longer per call either: it was 98% of a shaped run before row 2d4
//
// Build and run: tools/run-shaping-bench.sh [build-dir] [font.ttf] [iterations]

#include "font.hpp"
#include "shaper.hpp"
#include "ttf_font.hpp"

#include <hb-ot.h>
#include <hb.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// The string the -O0 figure was taken on: 24 characters of ordinary English, spaces included, so the
// run exercises a real GSUB pass rather than one ligature.
std::string const kRun24 = "The quick brown fox jump";

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Best of `repeats` passes of `iterations` calls. Best rather than mean: on a two-core box the mean is
// dominated by whatever else the machine was doing, and the question is what the code costs.
template <typename Fn>
double best_of(int iterations, int repeats, Fn&& fn)
{
    double best = 1e18;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i)
            fn();
        best = std::min(best, ms_since(start) / iterations);
    }
    return best;
}

// Mirrors content_hash() in src/rendering/ttf_font.cpp: the cost the face cache used to pay on every
// call, and now pays once per load. Measured here rather than exported because it is a price, not API.
uint64_t hash_font_data(std::vector<uint8_t> const& data)
{
    uint64_t hash = 1469598103934665603ull;
    for (auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<uint8_t> read_file(char const* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace

int main(int argc, char** argv)
{
    char const* font_path = argc > 1 ? argv[1] : std::getenv("AETHERIS_FONT");
    int const iterations = argc > 2 ? std::atoi(argv[2]) : 2000;
    if (!font_path) {
        std::fprintf(stderr, "usage: shaping-bench <font.ttf> [iterations]\n");
        return 2;
    }
    // active_font() is loaded once, from AETHERIS_FONT or a system path list. A process that starts
    // without either measures text with the bitmap fallback, and Font::measure() then looks *faster*
    // than shape_run() - which is how the previous session mistook "the shaper does not shape" for a
    // shaper bug. Set it here so measure() measures shaping.
    setenv("AETHERIS_FONT", font_path, 1);

    auto const bytes = read_file(font_path);
    if (bytes.empty()) {
        std::fprintf(stderr, "cannot read %s\n", font_path);
        return 2;
    }
    if (!aetheris::rendering::shaping_available()) {
        std::fprintf(stderr, "this build has no shaper\n");
        return 2;
    }

    // The identity the engine passes: a content hash computed once, when the font loads.
    aetheris::rendering::TtfFont loaded;
    if (!loaded.load(bytes)) {
        std::fprintf(stderr, "the engine cannot parse %s\n", font_path);
        return 2;
    }
    uint64_t const identity = loaded.identity();

    // A face and a font built once, the way a correct implementation keeps them, so that hb_shape below
    // is HarfBuzz and nothing else.
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<char const*>(bytes.data()), static_cast<unsigned>(bytes.size()),
        HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_font_t* font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, static_cast<int>(16.0f * 64.0f), static_cast<int>(16.0f * 64.0f));

    auto shape_with = [&](hb_font_t* f) {
        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, kRun24.data(), static_cast<int>(kRun24.size()), 0, static_cast<int>(kRun24.size()));
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(f, buffer, nullptr, 0);
        unsigned count = hb_buffer_get_length(buffer);
        hb_buffer_destroy(buffer);
        return count;
    };

    // Warm up: the first call through any of these paths faults in pages and builds HarfBuzz's internal
    // accelerators, and a single cold call would land in the first timing pass.
    for (int i = 0; i < 50; ++i) {
        (void)aetheris::rendering::shape_run(bytes, kRun24, 16.0f, identity);
        (void)shape_with(font);
    }

    unsigned const glyphs = shape_with(font);
    unsigned const glyphs_via_engine = static_cast<unsigned>(aetheris::rendering::shape_run(bytes, kRun24, 16.0f, identity).size());

    aetheris::rendering::StyleProperties style;
    style.set("font-size", "16px");
    auto const measured_font = aetheris::rendering::Font::from_style(style);
    float const width = measured_font.measure(kRun24);

    double const cached_ms = best_of(iterations, 5, [&] { (void)aetheris::rendering::shape_run(bytes, kRun24, 16.0f, identity); });
    double const measure_ms = best_of(iterations, 5, [&] { (void)measured_font.measure(kRun24); });
    double const hb_shape_ms = best_of(iterations, 5, [&] { (void)shape_with(font); });
    double const hb_font_create_ms = best_of(iterations, 5, [&] {
        hb_font_t* per_call = hb_font_create(face);
        hb_font_set_scale(per_call, 1024, 1024);
        hb_font_destroy(per_call);
    });

    // The miss path: every string here is one the cache has never seen, so each call pays a full shape
    // plus the insert. Pre-generated, so the loop measures the engine and not std::to_string.
    std::vector<std::string> unseen;
    unseen.reserve(iterations);
    for (int i = 0; i < iterations; ++i)
        unseen.push_back("The quick brown fox " + std::to_string(i));
    auto cold_start = Clock::now();
    for (auto const& text : unseen)
        (void)aetheris::rendering::shape_run(bytes, text, 16.0f, identity);
    double const cold_ms = ms_since(cold_start) / iterations;

    // The sink keeps the hash loop alive: a call nobody reads is a pure function of its input and the
    // compiler deletes the whole loop. The first version of this bench printed 0.0000 ms for a 756 KB
    // hash for exactly that reason.
    volatile uint64_t hash_sink = 0;
    double const hash_ms = best_of(iterations, 5, [&] { hash_sink = hash_font_data(bytes); });

    std::printf("font              %s (%zu bytes, upem %u, identity %016llx)\n", font_path, bytes.size(),
        hb_face_get_upem(face), static_cast<unsigned long long>(identity));
    std::printf("text              \"%s\" (%zu bytes, %u glyphs via HarfBuzz, %u via shape_run)\n",
        kRun24.c_str(), kRun24.size(), glyphs, glyphs_via_engine);
    std::printf("measure() width   %.4f px\n", width);
    std::printf("\n%-26s %10s\n", "step", "ms/run");
    std::printf("%-26s %10.4f\n", "shape_run, cached", cached_ms);
    std::printf("%-26s %10.4f\n", "Font::measure, cached", measure_ms);
    std::printf("%-26s %10.4f\n", "shape_run, cold", cold_ms);
    std::printf("%-26s %10.4f\n", "  hb_shape floor", hb_shape_ms);
    std::printf("%-26s %10.4f\n", "  hb_font_create", hb_font_create_ms);
    std::printf("%-26s %10.4f\n", "  font hash (once/load)", hash_ms);
    // A hit does no shaping at all, so this difference is the whole point of the cache: how much work
    // a page avoids the second time it measures the same string.
    std::printf("\ncached run is %.4f ms cheaper than shaping it (%.1fx)\n", hb_shape_ms - cached_ms,
        cached_ms > 0 ? hb_shape_ms / cached_ms : 0.0);
    std::printf("hash sink %llu\n", static_cast<unsigned long long>(hash_sink));

    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return 0;
}
