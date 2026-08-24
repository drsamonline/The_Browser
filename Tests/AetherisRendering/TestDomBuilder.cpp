#include <document.hpp>

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
    {
        auto document = Document::parse_html("<html><body><h1 id='title'>Hello</h1><p>World</p></body></html>");
        auto* html = document.root().first_child();
        require(html != nullptr && html->name == "html", "html element");
        auto* body = html->first_child();
        require(body != nullptr && body->name == "body", "body element");
        require(body->children.size() == 2, "body children");

        auto* heading = body->children[0].get();
        require(heading->name == "h1", "heading element");
        require(heading->attribute("id") && *heading->attribute("id") == "title", "heading attribute");
        require(heading->first_child()->data == "Hello", "heading text");
    }

    {
        auto document = Document::parse_html("<div><img src='logo.png'><br>after</div>");
        auto* div = document.root().first_child();
        require(div->children.size() == 3, "void elements remain siblings");
        require(div->children[0]->name == "img", "img element");
        require(div->children[1]->name == "br", "br element");
        require(div->children[2]->type == DomNodeType::Text, "text after void elements");
        require(div->children[2]->data == "after", "text content after void elements");
    }

    {
        auto document = Document::parse_html("<!-- note --><div>text</div>");
        require(document.root().children.size() == 2, "comment and element");
        require(document.root().children[0]->type == DomNodeType::Comment, "comment node");
        require(document.root().children[0]->data == " note ", "comment data");
    }

    return 0;
}
