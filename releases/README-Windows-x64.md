# Anim8orX Windows x64 Test Build

This is the first native Anim8orX editor-shell build.

Use it to verify:

- Default GUI launch.
- UniverseLib-style hierarchy, inspector, console, and tabbed editor layout.
- Anim8or-style Object, Figure, Sequence, and Scene modes.
- Unity-style viewport camera input path.
- Native `.an8` file loading.
- Object and mesh discovery.
- Point and face parsing.
- Quad-to-triangle geometry preparation.
- Default cube wireframe viewport.

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
