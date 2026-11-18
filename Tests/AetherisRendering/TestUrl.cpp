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

    std::cout << "url: " << (sizeof(cases) / sizeof(cases[0])) << " cases pass\n";
    return 0;
}
