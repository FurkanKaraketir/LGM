# Canvas editor

The canvas is the main drawing surface (`GraphScene` + `GraphView` in `src/canvas/`). Graphs are stored as JSON `.lgm` documents.

## Tools

| Tool | Shortcut | Action |
|------|----------|--------|
| Select | `Esc` | Click or rubber-band to select; drag to move |
| Add Node | `N` | Click empty canvas |
| Add Branch | `B` | Click source node, then target |
| Add Two-Port | `P` | Click two attachment nodes |
| Select Normal Tree | `Ctrl+Alt+T` | Click branches to build a manual tree (analysis mode) |

## Navigation

| Action | Input |
|--------|--------|
| Pan | Middle-mouse drag, or **Space** + left drag |
| Zoom | Mouse wheel (`Ctrl++` / `Ctrl+-`) |
| Go home | `Home` |

## Editing

| Action | Input |
|--------|--------|
| Delete selection | `Del` / `Backspace` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |
| Flip branch direction | `F` (selected branch) |
| Combine nodes | `M` (two selected nodes at same position) |
| Toggle two-port kind | `T` (transformer ↔ gyrator) |
| Select all | `Ctrl+A` |

Branches are directed (`from → to`). Multiple branches between the same node pair fan out perpendicular to the chord; each branch bows to the **left** of that direction.

## Docks and panels

- **Objects** — grouped tree of two-ports, nodes, and branches with icons; rename items inline; double-click to center the canvas.
- **Properties** — branch/two-port name, element constant, A/T/D type, active source flag, bow offset.
- **Analyze** — normal tree and state-space actions (see [analysis.md](analysis.md)).
- **State Space** — rendered equations after a successful derivation.

## Document format (`.lgm`)

JSON with top-level keys such as `branches`, `nodes`, `twoPorts`, `systemType`, and view metadata. Positions are scene coordinates; `serialId` gives stable identity across save/load.

Use **File → Open / Save** (`Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`). Example graphs live in [Examples/](../Examples/README.md).

## Settings

**Settings** (or toolbar) controls:

- Default **domain** for new branches and node naming
- Grid snap, spacing, visibility
- Theme (system / light / dark)
- Customizable keyboard shortcuts

## Curve tuning

In `src/canvas/canvas_items.cpp`:

- `kBowFactor` — arch height as a fraction of chord length (default `0.24`)
- `kLaneSpread` — gap between parallel branches (default `36`)

## Source files

| File | Role |
|------|------|
| `canvas.h` | Scene, items, modes, analysis hooks |
| `canvas_items.cpp` | Node, branch, two-port geometry and paint |
| `canvas_scene_input.cpp` | Mouse/keyboard interaction |
| `canvas_scene_graph.cpp` | Graph topology, normal tree UI, state-space trigger |
| `canvas_document.cpp` | `.lgm` load/save |
| `canvas_undo.cpp` | Undo stack commands |
| `canvas_view.cpp` | Zoom, pan, grid |
