# Anim8orX Windows x64 Test Build

This is the native Anim8orX editor-shell build with an Anim8or-style default workspace.

Use it to verify:

- Default GUI launch.
- Native Anim8or-like menu bar and compact command strip.
- Anim8orX logo in the command strip and window icon.
- File loading through `File > Open .an8...`, `Import`, drag-and-drop, and command-line paths.
- File saving through `File > Save`, `Save As...`, and `Export`.
- Slim left modeling rail with mode, coordinate, axis, tool, UV, shape, rig, and key groups.
- Separate selectable Hierarchy panel for scene graph mesh nodes.
- Anim8or-style Object, Figure, Sequence, and Scene modes.
- UniverseLib-style dock entry points for Explorer, Inspector, Console, Materials, and Timeline.
- Anim8orX property deck pages for Setup, View, Material, Object, Figure, Sequence, Scene, and Render.
- Unity-style viewport camera input path.
- RMB+mouse wheel fly-speed adjustment.
- World-space 3D grid and custom dark view selector.
- Optimized idle behavior: the viewport frame timer sleeps when nothing is moving.
- Optimized drawing path with cached GDI pens/brushes, persistent back buffer, cached camera projection basis, one mesh vertex projection pass per frame, filled face rendering, and selected-mesh wire budget limits.
- Optimized navigation repaint path: camera movement invalidates and blits only the viewport rectangle instead of redrawing the whole UI shell.
- Native `.an8` file loading for object/group/mesh and common primitive component chunks.
- Object, group, mesh, and helper-node discovery.
- Point parsing and Anim8or nested face point-data parsing.
- Quad-to-triangle geometry preparation.
- Working Build primitives/helpers: cube, sphere, cylinder, cone, torus, text helper, bone helper, camera helper, and light helper.
- Working Object actions: delete, extrude, inset, lathe, mirror, and face subdivision.
- Live viewport toggles for grid, axes, normals, backface culling, wireframe, and flat-shaded faces.
- Default cube shown as a filled flat-shaded object in a gray grid viewport.

## Run

Open PowerShell in this folder:

```powershell
.\Anim8orX.exe .\examples\cube.an8
```

You can also double-click `Anim8orX.exe`. With no command-line argument it
opens the editor and loads the bundled cube sample.

You can also pass another text-based `.an8` file path:

```powershell
.\Anim8orX.exe "C:\path\to\model.an8"
```

Bundled parser samples:

```powershell
.\Anim8orX.exe .\examples\nested-face.an8
.\Anim8orX.exe .\examples\primitive-components.an8
```
