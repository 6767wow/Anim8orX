# Anim8orX v0.1.0 Windows x64 Test Build

This is the first Anim8orX foundation build. It is not the full Vulkan editor yet.

Use it to verify:

- Native `.an8` file loading.
- Object and mesh discovery.
- Point and face parsing.
- Quad-to-triangle geometry preparation.
- View/projection camera math initialization.

## Run

Open PowerShell in this folder:

```powershell
.\Anim8orX.exe .\examples\cube.an8
```

You can also double-click `Anim8orX.exe`. With no command-line argument it
loads the bundled cube sample and waits before closing.

Expected output includes:

```text
Objects: 1
points=8 faces=6 triangles=12
Camera ready
```

You can also pass another text-based `.an8` file path:

```powershell
.\Anim8orX.exe "C:\path\to\model.an8"
```
