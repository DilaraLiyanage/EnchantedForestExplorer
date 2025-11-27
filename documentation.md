# Enchanted Forest — Portable Run Guide (Windows)

This guide explains how to run `EnchantedForest.exe` directly from `bin/Debug` by placing all required runtime libraries (DLLs) and assets beside the executable so it runs on any compatible Windows machine without installing toolchains.

## 1) Folder layout (keep these relative paths)

Place the following in the same folder as `EnchantedForest.exe` (typically `bin/Debug`):

- `EnchantedForest.exe`
- `forest.vert`
- `fragment_shader.glsl`
- `Models/` (directory) containing at least:
  - `fountain.obj`
  - `fountain.png`
  - `grass.png`, `moss.png`, `purple.png` (ground textures)
  - `path.png` (annulus + paths)
  - `trunk.png`, `leaves.png` (procedural tree textures)

Important: The program loads assets via relative paths (e.g., `Models/fountain.obj`). Do not flatten the `Models` directory. Keep the two shader files (`forest.vert`, `fragment_shader.glsl`) next to the exe.

## 2) Required runtime DLLs (place next to the exe)

Copy these DLLs next to `EnchantedForest.exe`:

Graphics and 3D libs
- `glew32.dll` (GLEW)
- `glfw3.dll` (GLFW)
- `freeglut.dll` (FreeGLUT, if linked dynamically)
- `assimp.dll` (Assimp importer; name may be `libassimp-*.dll` depending on your distribution)
- `zlib1.dll` (Zlib, used by Assimp)

MinGW/MSYS2 runtime (if your build is MinGW-based)
- `libstdc++-6.dll`
- `libgcc_s_seh-1.dll`
- `libwinpthread-1.dll`

Notes
- `opengl32.dll` and `glu32.dll` are provided by Windows; do not copy these.
- On MSYS2 setups, DLLs are typically under `C:\msys64\mingw64\bin`. On other installs, check your toolchain/lib folders.
- If your environment links any of the above statically, their DLL may not be needed.

## 3) Quick start (double‑click or PowerShell)

- Double‑click `EnchantedForest.exe`, or run from PowerShell with the working directory set to the exe folder:

```powershell
# From the project root
Set-Location "D:\SLIIT\Academics\Year3\Y3S1\GV\Assignment\OpenGL\Projects\EnchantedForest\bin\Debug"

# Run the app
.\EnchantedForest.exe
```

If the window opens but textures/paths don’t appear, verify the `Models` folder and shader files are present as shown above.

## 4) Troubleshooting

- “The code execution cannot proceed because XXX.dll was not found.”
  - Copy the missing DLL from your development machine (commonly `C:\msys64\mingw64\bin`) into the exe folder. The usual suspects are `glew32.dll`, `glfw3.dll`, `freeglut.dll`, `assimp*.dll`, `zlib1.dll`, and the MinGW runtime DLLs.
- Black/empty scene or missing textures
  - Ensure `forest.vert`, `fragment_shader.glsl`, and the entire `Models` directory are beside the exe.
  - Confirm the working directory is the exe folder (so relative paths resolve).
- Crash at start on another PC
  - Verify the GPU/driver supports OpenGL 3.x+ and that all required DLLs listed above are present.

## 5) Optional: Package a portable folder

To move the app to another computer, zip a folder with this structure:

```
Portable_Forest/
  EnchantedForest.exe
  forest.vert
  fragment_shader.glsl
  glew32.dll
  glfw3.dll
  freeglut.dll
  assimp.dll (name may vary)
  zlib1.dll
  libstdc++-6.dll
  libgcc_s_seh-1.dll
  libwinpthread-1.dll
  Models/
    fountain.obj
    fountain.png
    grass.png
    moss.png
    purple.png
    path.png
    trunk.png
    leaves.png
```

Extract anywhere and run `EnchantedForest.exe`. No installation required.

## 6) Controls (quick reminder)

- View: `V`
- Paths style: `P`
- Fountain radius (2D overlay): `[` / `]`
- Ground textures: `T` / `M`
- Trees: `I`/`O` scale, `J` yaw
- Fountain: `K`/`L` scale, `U` yaw
- Plant tree: Left mouse click
- Reset: `R` (full reset)
- Exit: `ESC`

Enjoy exploring the Enchanted Forest! ✨
