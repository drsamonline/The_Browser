#include "document.hpp"
#include "form_runtime.hpp"

#include <cassert>
#include <iostream>

using namespace aetheris::rendering;

int main() {
    auto document = Document::parse_html(R"(
        <form action="/search" method="get">
          <input name="q" value="hello">
          <input type="checkbox" name="images" value="1" checked>
          <input type="radio" name="mode" value="all" checked>
          <input type="radio" name="mode" value="news">
          <textarea name="note">abc</textarea>
          <select name="lang"><option value="en" selected>English</option><option value="fr">French</option></select>
          <input type="submit" value="Search">
        </form>
    )");

    FormRuntime runtime(document);
    assert(runtime.control_count() == 7);
    assert(runtime.focus(0));
    assert(runtime.insert_text(" world"));
    assert(runtime.control(0)->value == "hello world");
    assert(runtime.backspace());
    assert(runtime.control(0)->value == "hello worl");
    assert(runtime.activate(3));
    assert(!runtime.control(2)->checked && runtime.control(3)->checked);
    assert(runtime.set_value(5, "fr"));

    auto url = Url::parse("https://example.test/docs/page.html");
    assert(url.is_valid());
    auto submission = runtime.submit(6, url);
    assert(submission.has_value());
    assert(submission->method == FormSubmission::Method::Get);
    assert(submission->action.serialized() == "https://example.test/search");
    assert(submission->encoded_body.find("q=hello+worl") != std::string::npos);
    assert(submission->encoded_body.find("images=1") != std::string::npos);
    assert(submission->encoded_body.find("mode=news") != std::string::npos);
    assert(submission->encoded_body.find("lang=fr") != std::string::npos);

    auto post_document = Document::parse_html(R"(<form action="submit" method="post"><input name="a" value="b"><button type="submit">Go</button></form>)");
    FormRuntime post_runtime(post_document);
    auto post = post_runtime.submit(1, url);
    assert(post.has_value());
    assert(post->method == FormSubmission::Method::Post);
    assert(post->action.serialized() == "https://example.test/docs/submit");
    assert(post->encoded_body == "a=b");

    std::cout << "Aetheris form runtime tests passed\n";
    return 0;
}
