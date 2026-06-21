# Anim8orX Step 1 Blueprint

This document defines the first stable brick for Anim8orX: a retained-mode UI framework, a Vulkan-backed viewport boundary, Unity-style camera navigation, and a native `.an8` loader that can feed the scene hierarchy.

## 1. Retained-Mode UI Architecture

Anim8orX should treat UI as a retained object tree, not an immediate-mode draw stream. Each widget persists across frames, owns stable state, and participates in layout, hit-testing, input routing, and rendering.

### Core Layers

1. Platform Layer
   - Owns the OS window, keyboard/mouse events, clipboard, DPI scale, cursors, and Vulkan surface creation.
   - Produces a normalized `InputFrame` every frame.

2. Renderer Layer
   - Owns Vulkan device resources, swapchain, command buffers, descriptor pools, pipelines, and frame synchronization.
   - Exposes a narrow UI drawing API: rectangles, borders, text runs, scissor regions, icon quads, and viewport image/present targets.

3. UI Framework Layer
   - Owns the retained widget tree.
   - Performs measure, arrange, hit-test, focus, capture, and draw traversal.
   - Never directly owns Vulkan swapchain objects. It submits draw packets into renderer-owned frame buffers.

4. Editor Shell Layer
   - Defines Anim8orX windows and panels: top tabs, hierarchy, inspector, console, asset browser, toolbar, status bar, and viewport.
   - Bridges selected scene objects into inspector property editors.

5. Scene/Document Layer
   - Owns objects, meshes, transforms, materials, animations, and undoable commands.
   - UI reads through view models and mutates through explicit editor commands.

### Retained Widget Base

Each UI object should inherit from a small base class:

```cpp
class Widget {
public:
    virtual Size Measure(const LayoutConstraints& c) = 0;
    virtual void Arrange(const Rect& finalRect) = 0;
    virtual HitResult HitTest(Vec2 p) const;
    virtual bool OnInput(const InputEvent& e);
    virtual void BuildDrawList(UiDrawList& out) const = 0;

    Rect Bounds() const;
    void SetVisible(bool visible);
    void AddChild(std::unique_ptr<Widget> child);
};
```

The important rule: layout and drawing are separate. Widgets compute geometry first, then emit draw commands later. This keeps Vulkan integration predictable because all UI geometry can be batched after layout.

### Layout System

Use a small set of deterministic layout containers instead of free-floating editor windows:

- `DockRoot`: divides the app into top bar, main workspace, bottom status/console strip.
- `TabBar`: persistent top navigation tabs such as Viewport, Hierarchy, Inspector, Console.
- `SplitPanel`: resizable left/right or top/bottom regions.
- `FlexPanel`: horizontal/vertical flow with grow/shrink weights.
- `GridPanel`: fixed rows/columns for dense inspector and property editing.
- `ScrollPanel`: clips and scrolls large hierarchies and logs.
- `CollapsiblePanel`: UniverseLib-style bordered sections with header, arrow, and content.

Default editor layout:

```text
+----------------------------------------------------------------+
| TopNavigation: Object | Figure | Sequence | Scene | Console     |
+--------------------+-------------------------------+-----------+
| Hierarchy          | Vulkan Viewport               | Inspector |
| Object Explorer    | camera controls integrated    | Transform |
| Scene Graph        | into this panel                | Mesh data  |
+--------------------+-------------------------------+-----------+
| Console / Log / Command line                                    |
+----------------------------------------------------------------+
```

### UniverseLib-Style Visual Language

- Dark charcoal backgrounds with explicit panel borders.
- Monospace or programmer-friendly font for hierarchy, inspector, console, and numeric fields.
- Subtle contrast, not glossy styling.
- Collapsible headers use clear text labels and small arrow icons.
- Inspector rows use two-column grid: property label left, editable field right.
- Hierarchy is text-driven and strict: parent/child indentation, icons, selected row highlight.

### Input Routing

Input should route in this order:

1. Captured widget, such as a dragging splitter or active text box.
2. Focused widget, such as text input consuming keyboard.
3. Hit-tested widget under cursor.
4. Viewport controller if cursor is inside the viewport and no UI control consumed the event.
5. Global shortcuts.

The viewport is a widget, but it owns a 3D input controller instead of ordinary UI behavior. This is where RMB+WASD, Alt+mouse orbit/pan, scroll dolly, and F focus are handled.

### Font Rendering With Vulkan

Start with one robust font path:

- Use FreeType or stb_truetype to bake a font atlas.
- Store glyph metrics in CPU memory.
- Upload atlas as a Vulkan sampled image.
- UI text draw packets contain glyph quads with atlas UVs.
- Batch text and rectangles into a dynamic per-frame vertex buffer.

Later, add SDF/MSDF fonts for scalable text. The initial retained-mode framework only needs crisp 12-16 px text.

### Vulkan Integration Boundary

The UI layer should emit backend-agnostic draw packets:

```cpp
struct UiDrawCmd {
    UiPrimitiveType type;
    Rect clipRect;
    Color color;
    TextureHandle texture;
    uint32_t firstVertex;
    uint32_t vertexCount;
};
```

The Vulkan renderer consumes these packets and owns:

- UI pipeline.
- Textured and solid-color draw variants.
- Scissor setup from `clipRect`.
- Dynamic vertex/index buffers.
- Font atlas descriptors.
- Viewport render targets.

The viewport itself should render into either the swapchain region directly with scissor/viewport state or into an offscreen color/depth target displayed by the UI compositor. Offscreen is cleaner for future overlays, selection outlines, and gizmos.

## 2. Editor Scene Model

Use a strict scene graph from day one:

```cpp
struct SceneNode {
    NodeId id;
    std::string name;
    Transform local;
    NodeId parent;
    std::vector<NodeId> children;
    std::vector<ComponentId> components;
};
```

Components should include:

- `MeshRendererComponent`
- `SkeletonComponent`
- `AnimationClipComponent`
- `CameraComponent`
- `LightComponent`

The `.an8` loader creates document objects, mesh assets, and scene nodes. The hierarchy panel reads nodes. The inspector edits transform and component data through command objects, not raw pointer mutation.

## 3. Step 1 Code Bricks

The companion files in this output folder are:

- `Anim8orXCamera.hpp`: self-contained C++17 math and Unity-style viewport camera controller.
- `An8Parser.hpp`: self-contained C++17 lexer/parser for basic `.an8` objects, meshes, points, and faces.

These are deliberately renderer-agnostic. Vulkan code should consume the camera's view/projection matrices and the parser's triangle-ready mesh data.
