"# Aetheris Rendering Engine Implementation Plan

## Overview
This document outlines the implementation plan for the Aetheris HTML/CSS rendering engine, which will enable the browser to display web content.

## Architecture

```
┌───────────────────────────────────────────────────────┐
│                   Rendering Engine                    │
├───────────────────┬───────────────────┬───────────────┤
│   HTML Parser     │    CSS Engine     │  Layout Engine│
├─────────┬─────────┼─────────┬─────────┼───────┬───────┤
│ Token-  │ DOM     │ CSS     │ Style   │ Box   │ Text  │
│ izer    │ Builder │ Parser  │ System  │ Model │ Layout│
└─────────┴─────────┴─────────┴─────────┴───────┴───────┘
```

## HTML Parser Implementation

### 1. Tokenizer (1 week)
**Files**:
- `src/rendering/html_tokenizer.hpp`
- `src/rendering/html_tokenizer.cpp`

**Implementation Details**:
- State machine-based tokenizer
- Handle basic HTML5 tokens:
  - DOCTYPE
  - Start tags
  - End tags
  - Text content
  - Comments
  - Self-closing tags
- Minimal memory allocations
- Error recovery for malformed HTML

**Example Token Types**:
```cpp
enum class HtmlTokenType {
    Doctype,
    StartTag,
    EndTag,
    SelfClosingTag,
    Text,
    Comment,
    EOFToken
};

struct HtmlToken {
    HtmlTokenType type;
    std::string_view data;
    std::unordered_map<std::string_view, std::string_view> attributes;
    // Position information for error reporting
    size_t line;
    size_t column;
};
```

### 2. DOM Builder (1 week)
**Files**:
- `src/rendering/dom.hpp`
- `src/rendering/dom.cpp`

**Implementation Details**:
- Minimal DOM implementation:
  - Node base class
  - Element nodes
  - Text nodes
  - Document node
- Tree construction from tokens
- Basic DOM manipulation methods
- Memory-efficient storage

**Example DOM Classes**:
```cpp
class Node {
public:
    virtual ~Node() = default;
    virtual NodeType type() const = 0;
    // ... common methods
};

class Element : public Node {
public:
    NodeType type() const override { return NodeType::Element; }
    std::string_view tag_name() const { return m_tag_name; }
    const std::vector<std::unique_ptr<Node>>& children() const { return m_children; }
    // ... element-specific methods
private:
    std::string m_tag_name;
    std::vector<std::unique_ptr<Node>> m_children;
    // ... attributes, etc.
};

class Document : public Node {
public:
    NodeType type() const override { return NodeType::Document; }
    Element* document_element() { return m_document_element.get(); }
    // ... document methods
private:
    std::unique_ptr<Element> m_document_element;
};
```

## CSS Engine Implementation

### 1. CSS Parser (1 week)
**Files**:
- `src/rendering/css_parser.hpp`
- `src/rendering/css_parser.cpp`

**Implementation Details**:
- CSS tokenization
- Rule parsing (selectors + declarations)
- Basic property value parsing
- Minimal error recovery
- At-rule support (limited)

**Example CSS Data Structures**:
```cpp
struct CSSRule {
    std::vector<CSSSelector> selectors;
    std::vector<CSSProperty> properties;
};

struct CSSSelector {
    std::string value;
    // Specificity calculation
    uint32_t specificity() const;
};

struct CSSProperty {
    std::string name;
    std::string value;
    bool important;
};
```

### 2. Style System (1 week)
**Files**:
- `src/rendering/style_system.hpp`
- `src/rendering/style_system.cpp`

**Implementation Details**:
- Selector matching
- Specificity calculation
- Property inheritance
- Style resolution
- Computed style generation

**Example Style System**:
```cpp
class StyleResolver {
public:
    // Apply styles to a DOM tree
    void apply_styles(Document* document, const std::vector<CSSRule>& rules);

    // Get computed style for an element
    ComputedStyle get_computed_style(Element* element);

private:
    // Selector matching
    bool matches_selector(Element* element, const CSSSelector& selector);

    // Style cascade
    void cascade_styles(Element* element, const std::vector<CSSProperty>& properties);
};
```

## Layout Engine Implementation

### 1. Box Model (1 week)
**Files**:
- `src/rendering/box_model.hpp`
- `src/rendering/box_model.cpp`

**Implementation Details**:
- Box tree construction
- Box types (block, inline, inline-block)
- Box dimensions and positioning
- Margin collapsing
- Basic flow layout

**Example Box Model**:
```cpp
class LayoutBox {
public:
    enum class Type { Block, Inline, InlineBlock };

    Type type() const { return m_type; }
    const Rect& content_rect() const { return m_content_rect; }
    const Rect& margin_rect() const { return m_margin_rect; }
    const Rect& padding_rect() const { return m_padding_rect; }
    const Rect& border_rect() const { return m_border_rect; }

    // Layout methods
    void calculate_dimensions(const ComputedStyle& style);
    void position(const Point& position);

private:
    Type m_type;
    Rect m_content_rect;
    Rect m_padding_rect;
    Rect m_border_rect;
    Rect m_margin_rect;
    std::vector<std::unique_ptr<LayoutBox>> m_children;
};
```

### 2. Text Layout (1 week)
**Files**:
- `src/rendering/text_layout.hpp`
- `src/rendering/text_layout.cpp`

**Implementation Details**:
- Font management
- Text measurement
- Line breaking
- Text alignment
- Basic text rendering

**Example Text Layout**:
```cpp
class TextLayout {
public:
    // Measure text dimensions
    Size measure_text(std::string_view text, const ComputedStyle& style);

    // Layout text into lines
    std::vector<LineBox> layout_text(std::string_view text,
                                   const ComputedStyle& style,
                                   float available_width);

    // Render text
    void render_text(RenderingContext& context,
                    std::string_view text,
                    const Point& position,
                    const ComputedStyle& style);

private:
    // Font management
    Font* get_font(const ComputedStyle& style);
};
```

## Integration with WindowManager

### 1. Rendering Pipeline (1 week)
**Files**:
- `src/rendering/rendering_context.hpp`
- `src/rendering/renderer.hpp`

**Implementation Details**:
- Integration with WindowManager
- Painting algorithm
- Layer management
- Damage tracking
- Platform-specific rendering backends

**Example Renderer**:
```cpp
class Renderer {
public:
    explicit Renderer(ui::WindowManager& window);

    // Layout and render a document
    void render_document(Document* document);

    // Handle window resize
    void handle_resize(int width, int height);

private:
    // Layout phase
    void layout_document(Document* document);

    // Paint phase
    void paint_document(Document* document);

    // Platform-specific rendering
    void render_box(const LayoutBox& box);
    void render_text(const TextLayout& text, const Point& position);

    ui::WindowManager& m_window;
    std::unique_ptr<RenderingContext> m_context;
};
```

## Testing Strategy

### 1. Unit Tests
- HTML tokenizer tests
- DOM construction tests
- CSS parser tests
- Selector matching tests
- Layout tests

### 2. Integration Tests
- HTML + CSS rendering tests
- Layout verification
- Performance tests

### 3. Test Pages
Create a set of test HTML/CSS files:
- Basic HTML structure
- CSS selectors
- Box model tests
- Text layout tests
- Positioning tests

## Implementation Order

1. **HTML Tokenizer** → **DOM Builder**
2. **CSS Parser** → **Style System**
3. **Box Model** → **Text Layout**
4. **Rendering Pipeline** integration
5. Testing and optimization

## Performance Considerations

1. **Memory Efficiency**:
   - Minimize allocations during parsing
   - Reuse objects where possible
   - Efficient data structures

2. **Speed Optimizations**:
   - Incremental layout
   - Dirty bit tracking
   - Minimal style recalculations

3. **Rendering Optimizations**:
   - Layer management
   - Damage tracking
   - Hardware acceleration where possible

## Next Steps

1. Create implementation files for HTML tokenizer
2. Implement basic tokenization
3. Create test cases for HTML parsing
4. Implement DOM construction
5. Begin CSS parser implementation"