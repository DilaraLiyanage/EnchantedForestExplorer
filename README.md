# Enchanted Forest — 2D/3D Algorithmic Demo

An interactive application blending procedural 2D artwork with an immersive 3D scene. It demonstrates:

- Basic OpenGL lines (2D overlay grid, Bresenham path outlines, midpoint circle ring)
- Bresenham's line algorithm (discrete layout paths)
- Midpoint circle algorithm (fountain annulus and overlay ring)
- 3D model with texture mapping (OBJ fountain textured by `Models/fountain.png`)

## Overview

The world XZ plane spans `[-10,10] × [-10,10]`. A discrete design grid (default `50 × 50`) maps to this world. Paths are generated with Bresenham from sampled forest cells back to the fountain at the grid center. A star-shaped hedge made of triangular prisms creates forbidden zones. Trees auto-populate evenly around allowed areas with adaptive spacing.

The 2D view uses basic OpenGL line primitives and filled quads to show:
- Grid and border
- Fountain core and annulus (yellow)
- Bresenham paths (brown), skipping forbidden cells
- Tree markers (dark green)
- Hedge footprints (masked)

The 3D view renders:
- Ground plane with switchable textures (grass/moss/purple)
- Accurate path ribbon mesh built from the Bresenham grid
- OBJ fountain (yaw-only rotation, scalable) with `fountain.png`
- Star hedge wedges (moss texture)
- Procedural trees (textured trunk/leaves), plus ambient fireflies

## Controls

- `V`: Toggle 2D / 3D view (window title shows view)
- `P`: Cycle stylized path mesh (visual only); accurate user paths are always rendered when available
- `[` / `]`: Decrease / increase fountain pixel radius (affects ring and overlays)
- `T` / `M`: Cycle ground textures forward / backward
- `I` / `O`: Trees scale up / down
- `J`: Trees yaw-left
- `K` / `L`: Fountain scale up / down (clamped to avoid hedge collision)
- `U`: Fountain yaw-right (yaw-only)
- `Mouse Left`: Plant a tree at cursor if allowed
- `R`: Reset camera and transforms
- `ESC`: Exit

## Algorithms

### Bresenham's Line (grid → world)
- Inputs: start/end grid cells `(x0,y0) → (x1,y1)`
- Output: inclusive sequence of grid cells along the discrete line
- Integration:
  - Build accurate path mesh by emitting small quads between successive cell centers
  - Skip segments whose midpoints lie in forbidden zones (hedge wedges or outer disk)
  - 2D overlay paints path cells and can outline with `GL_LINES`

### Midpoint Circle
- Used to sample the fountain ring (annulus) at sufficient angular resolution
- Converts sampled points to world positions; UVs are world-aligned so `path.png` tiles exactly one per design-grid cell
- 2D overlay uses the same radius mapping to draw the ring in yellow

## Constraints & Guards

- Forbidden zones: any point within the hedges' outer radius or inside wedge triangle footprints
- Tree placement: avoids paths and forbidden zones; enforces spacing; adapts spacing if area is constrained to approach the requested count
- Fountain scaling: clamped before colliding with hedges; hedges follow fountain scale; trees inside hedge disk are pushed outward

## Presentation Tips

1. Start in 2D:
    - Show the grid, draw a Bresenham path (highlight cells), outline with `GL_LINES`
    - Draw the midpoint circle ring; explain grid→world mapping
2. Flip to 3D:
    - Walk the accurate path ribbon; rotate/scale the fountain (observe yaw-only and clamp)
    - Showcase wedge hedges and tree constraints (try planting near forbidden zones)
3. Summarize:
    - How algorithms drive both visualizations
    - Why UVs are world-aligned (1:1 tiling to design grid)

## Code Organization (single-file notes)

- `main.cpp` contains:
  - Layout generation (Bresenham, forbidden checks)
  - Ring construction (midpoint circle)
  - 2D overlay (grid, lines, filled quads)
  - 3D rendering (ground, fountain OBJ, wedges, trees, ring)
  - Input handling and guards

> This submission intentionally avoids refactoring into multiple files (as requested). Comments identify algorithm sites, mapping logic, and guard behaviors for clarity and grading.

## Assets

- `Models/fountain.obj`, `Models/fountain.png`: Fountain model and texture
- `Models/path.png`: Path texture (tiles 1:1 over design grid)
- `Models/grass.png`, `Models/moss.png`, `Models/purple.png`: Ground and hedge textures
- `Models/trunk.png`, `Models/leaves.png`: Procedural tree textures

## Notes

- Runs in Code::Blocks with GLEW/GLFW/OpenGL set up
- Shader files: `forest.vert` and `fragment_shader.glsl`
- Window title reflects the active view for presentation clarity

## Rubric Alignment

- Technical Implementation: Algorithms correct and demonstrated; 2D/3D integration; guards for stability
- Creativity & Design: Mystic garden concept; cohesive constraints; ambient elements
- Presentation: Clear README; demo script; informative logs and titles
- Code Quality: Inline documentation; consistent naming; organized logic without excessive globals in critical paths
