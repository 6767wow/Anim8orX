# Anim8orX

Anim8orX is a modern, open-source spiritual successor to the legacy 3D modeling workflow of Anim8or. The goal is a fast native C++ editor with a Vulkan viewport, a retained-mode object-oriented UI framework, Unity-style viewport controls, and native text-based `.an8` import.

This repository currently contains the first foundational brick:

- Retained-mode UI architecture blueprint inspired by UniverseLib-style object explorers and inspectors.
- Self-contained C++17 viewport camera controller with RMB+WASD fly, Alt+LMB orbit, Alt+MMB pan, scroll dolly, and F focus support.
- Self-contained C++17 `.an8` lexer/parser for objects, meshes, points, faces, and triangle-list geometry preparation.
- A small `Anim8orX` foundation executable that loads an `.an8` file and prints parsed geometry stats.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run the parser sandbox:

```powershell
.\build\Release\Anim8orX.exe .\examples\cube.an8
```

The packaged Windows build can also be double-clicked. If no file argument is
provided, it loads the bundled `examples/cube.an8` sample and waits before
closing so the output is visible.

On single-config generators, the executable may be under `build\Anim8orX.exe`.

## Repository Layout

```text
include/Anim8orX/Viewport/Camera.hpp     Unity-style viewport camera math
include/Anim8orX/Import/An8Parser.hpp    Native .an8 parser and triangulation
docs/architecture/Step1_Blueprint.md     UI and engine architecture blueprint
examples/cube.an8                        Minimal parser test asset
src/main.cpp                             Foundation CLI test harness
```

## Roadmap

1. Vulkan bootstrap: instance, device, swapchain, command buffers, depth target.
2. UI renderer: rectangles, borders, scissor clips, font atlas, retained widget tree.
3. Editor shell: top navigation, hierarchy, inspector, console, viewport panel.
4. Scene graph: nodes, transforms, components, selection, undoable commands.
5. Mesh viewport: upload imported `.an8` geometry into Vulkan vertex/index buffers.

## License

MIT. See `LICENSE`.
