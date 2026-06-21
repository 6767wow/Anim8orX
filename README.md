# Anim8orX

Anim8orX is a modern, open-source spiritual successor to the legacy 3D modeling workflow of Anim8or. The goal is a fast native C++ editor with a Vulkan viewport, a retained-mode object-oriented UI framework, Unity-style viewport controls, and native text-based `.an8` import.

This repository currently contains the first editor-shell brick with an Anim8or-style default workspace:

- Native Windows GUI launch instead of a command-line harness.
- Native menu surface: File, Edit, Mode, Object, Options, View, Build, Scripts, Render, Window, About.
- Anim8or-style compact command strip, Object/Figure/Sequence/Scene tabs, left modeling tool rail, gray grid viewport, orange active labels, and bottom object/status strip.
- Anim8orX-branded property deck with Setup, View, Material, Object, Figure, Sequence, Scene, and Render pages.
- Branded `wlogo.png` logo displayed in the app chrome and used as the window icon.
- World-space 3D grid in the viewport with camera-projected floor/depth guides.
- Real Hierarchy panel next to the tool rail with selectable imported/generated mesh nodes.
- Optimized retained GDI viewport shell with cached pens/brushes, persistent back buffer, cached projection basis, per-frame mesh vertex projection, viewport-only dirty repaints during navigation, filled face rendering by default, selected-mesh wire budget limits, and no idle repaint timer.
- Custom dark in-viewport view selector for All, Front, Back, Left, Right, Top, Bottom, Ortho, and Perspective.
- File loading through `File > Open .an8...`, `Import`, drag-and-drop `.an8` files, command-line file arguments, or the bundled sample.
- File saving through `File > Save`, `Save As...`, and `Export` for current mesh documents.
- UniverseLib-inspired dock entry points and bottom dock for Explorer, Inspector, Console, Materials, and Timeline.
- Working Build commands for cube, sphere, cylinder, cone, torus, text helper, bone helper, camera helper, and light helper nodes.
- Working Object commands for delete, extrude, inset, lathe, mirror, and face subdivision.
- Visible parameter surface for live grid/snap preferences, viewport display flags, wireframe/flat-shaded toggles, material shader values, primitive dimensions, modifier inputs, bone/DOF constraints, sequence interpolation, scene actor/camera/light controls, and render output options.
- Default cube viewport rendered as a filled flat-shaded object at launch.
- Self-contained C++17 viewport camera controller with RMB+WASD fly, RMB+mouse wheel fly-speed adjustment, Alt+LMB orbit, Alt+MMB pan, scroll dolly, and F focus support.
- Self-contained C++17 `.an8` lexer/parser for objects, groups, meshes, primitive components, points, nested face point-data, and triangle-list geometry preparation.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run the editor:

```powershell
.\build\Release\Anim8orX.exe .\examples\cube.an8
```

The packaged Windows build can also be double-clicked. If no file argument is
provided, it loads the bundled `examples/cube.an8` sample in the default editor viewport.

For automated verification:

```powershell
.\build\Release\Anim8orX.exe --smoke-test
.\build\Release\Anim8orX.exe --smoke-test .\examples\nested-face.an8
.\build\Release\Anim8orX.exe --smoke-test .\examples\primitive-components.an8
```

On single-config generators, the executable may be under `build\Anim8orX.exe`.

## Repository Layout

```text
include/Anim8orX/Viewport/Camera.hpp     Unity-style viewport camera math
include/Anim8orX/Import/An8Parser.hpp    Native .an8 parser and triangulation
docs/architecture/Step1_Blueprint.md     UI and engine architecture blueprint
examples/cube.an8                        Minimal parser test asset
examples/nested-face.an8                 Nested Anim8or face point-data parser test
examples/primitive-components.an8         Cube/sphere/cylinder component parser test
assets/wlogo.png                         Anim8orX logo asset
src/main.cpp                             Native editor shell
```

## Roadmap

Anim8orX is targeting a full Anim8or revamp, not just an importer:

1. Object editor: primitives, point/edge/face selection, extrude, lathe, bevels, subdivision surfaces, materials, UV basics.
2. Figure editor: bones, weights, hierarchy, IK-ready transform model.
3. Sequence editor: loop authoring, keyframes, curves, playback.
4. Scene editor: layout, cameras, lights, render blocking, shot timeline.
5. Modern layer: Vulkan viewport, retained-mode UI renderer, command stack, asset browser, console, inspector, plugin-friendly internals.

## License

MIT. See `LICENSE`.
