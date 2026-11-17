# Upstream first

**Read `Libraries/LibWeb` before writing anything interaction-shaped.** This engine
is derived from Ladybird, so focus, hover, `:active`, event bubbling and style
invalidation have already been solved properly there. Re-deriving them has cost
this project whole sessions; porting them has landed first time.

## The rule

1. **Find it upstream before designing it.** Sparse-clone LibWeb and search it.
   Port the shape — data flow, order of operations, names — not just the outcome.
2. **Read the caller, not just the callee.** `relayout()` looked correct in
   isolation and silently discarded the interaction state; the fault was visible
   only by reading what called it.
3. **When upstream has no answer, say so** in `DESIGN.md` and record why.
4. **Fix the function that drops the state, not the call site that notices.** Two
   sessions were spent patching the focus call site while `relayout()` was the
   culprit.

## Getting the reference

    git clone --depth 1 --filter=blob:none --sparse https://github.com/LadybirdBrowser/ladybird.git
    cd ladybird && git sparse-checkout set Libraries/LibWeb

## Where to look

| Problem | Read |
| --- | --- |
| focus, `:focus`, `:focus-visible`, `:focus-within` | `DOM/Document.cpp` (`set_focused_area`), `DOM/Element.cpp` (`is_focused`, `should_indicate_focus`) |
| hover and active state | `DOM/Document.cpp` (`set_hovered_node`), `DOM/Element.cpp` (`is_being_activated`) |
| pseudo-class matching | `CSS/SelectorMatching.cpp` (`ENUMERATE_ELEMENT_STATES`) |
| style invalidation after a state change | `CSS/Invalidation` (`invalidate_style_after_pseudo_class_state_change`) |
| event dispatch and bubbling to document/window | `Page/EventHandler.cpp` |
| scroll state | `DOM/Document.cpp`, `Page/EventHandler.cpp` |
| layout, box model | `Layout/` |

## Where things are in this tree

| You want to change | Read first |
| --- | --- |
| entry point, modes, chrome document | `src/main.cpp` |
| HTML to DOM | `src/rendering/document.cpp` |
| CSS parse, cascade, matching | `src/rendering/css_parser.cpp`, `style.cpp` |
| layout, units, shorthands | `src/rendering/layout.cpp` |
| paint commands, raster | `src/rendering/render_tree.cpp`, `software_surface.cpp`, `paint_executor.cpp` |
| interaction state | `src/rendering/visual_state.{hpp,cpp}` |
| JavaScript host, DOM bindings | `src/js/js.cpp` |
| networking, TLS | `src/network/` |
| window, input routing, session | `src/ui/` |

## What this is worth, in evidence

One day on this repo:

- Three bugs — the window running no page scripts, clicks never reaching handlers,
  `:focus` never painting — were each machinery that existed with nothing feeding
  it. Each cost a session of guessing.
- `:focus` was fixed only after reading upstream, and the fault was
  `relayout()` rebuilding layout without the interaction state.
- Every feature ported from LibWeb — `:focus-within`, `document.activeElement`, the
  focus transition order, keys bubbling from the focused element — landed first
  time with a pixel-level test.

## Check the instrument before the subject

Reading upstream first is half of it. The other half is not blaming the engine for a measurement that is
wrong. On 2026-11-17, three investigations in Aetheris blamed the browser and were wrong three times — a
pixel sample read as a picture, a verdict string a script computed backwards, an assertion whose colour
test also matched white. The engine was correct throughout.

Before concluding anything from a measurement:

1. Print the raw value, never a derived verdict (`print(pixel)`, not `print("SIDE-BY-SIDE")`).
2. Test a colour as that colour, never as "not the other one" — white matches most careless tests.
3. Look at the rendered frame before reasoning from coordinates.
4. A control case must be able to come out the other way; if it cannot, it proves nothing.

