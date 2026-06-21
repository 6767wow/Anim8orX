# Anim8orX Windows x64 Test Build

This is the native Anim8orX editor-shell build with an Anim8or-style default workspace.

Use it to verify:

- Default GUI launch.
- Native Anim8or-like menu bar and compact command strip.
- Anim8orX logo in the command strip and window icon.
- File loading through `File > Open .an8...` and drag-and-drop.
- Slim left modeling rail with mode, coordinate, axis, tool, UV, shape, rig, and key groups.
- Anim8or-style Object, Figure, Sequence, and Scene modes.
- UniverseLib-style dock entry points for Explorer, Inspector, Console, Materials, and Timeline.
- Anim8orX property deck pages for Setup, View, Material, Object, Figure, Sequence, Scene, and Render.
- Unity-style viewport camera input path.
- World-space 3D grid and custom dark view selector.
- Native `.an8` file loading.
- Object and mesh discovery.
- Point and face parsing.
- Quad-to-triangle geometry preparation.
- Default cube wireframe in a gray grid viewport.

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
