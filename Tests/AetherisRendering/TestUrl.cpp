#include <url.hpp>
#include <url_path.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace aetheris::rendering;

static void require(bool condition, std::string const& message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    struct Case {
        char const* path;
        char const* expected;
        char const* what;
    };
    Case const cases[] {
        // Upstream's own note: "/usr/.." is "/", not a lack of a path.
        { "/usr/..", "/", "a trailing double dot pops the last segment" },
        { "/usr/local/..", "/usr/", "a double dot pops one segment" },
        { "/deep/dir/../up", "/deep/up", "a double dot resolves in the middle" },
        { "/deep/dir/../../up", "/up", "two double dots pop two segments" },
        { "/a/./b", "/a/b", "a single dot is dropped" },
        { "/a/../..", "/", "a double dot past the root stays at the root" },
        { "/a/b/", "/a/b/", "a trailing slash is kept" },
        { "/", "/", "the root stays the root" },
        { "/a//b", "/a/b", "a doubled slash keeps one separator" },
    };
    for (auto const& test : cases)
        require(remove_dot_segments(test.path) == test.expected, test.what);

    // Url::resolve with a ".." that cannot go above the root. This hung the browser: `pos - 1`
    // underflowed when the ".." was the first segment, `rfind` then searched from the end, and the
    // replace was a no-op - an infinite loop on base "/up" with reference "../up".
    {
        auto base = Url::parse("http://example.com/up");
        require(Url::resolve(base, "../up").serialized() == "http://example.com/up",
            "a dot dot at the root is dropped, not looped on");
        auto deep = Url::parse("http://example.com/deep/dir/page");
        require(Url::resolve(deep, "../up").serialized() == "http://example.com/deep/up",
            "a dot dot pops one segment");
        require(Url::resolve(deep, "../../up").serialized() == "http://example.com/up",
            "two dot dots pop two");
        auto root = Url::parse("http://example.com/");
        require(Url::resolve(root, "../up").serialized() == "http://example.com/up",
            "a dot dot from the root resolves at the root");
    }

    std::cout << "url: " << (sizeof(cases) / sizeof(cases[0])) << " cases pass\n";
    return 0;
}
