# Canvas editor

The canvas is the main drawing surface (`GraphScene` + `GraphView` in `src/canvas/`). Graphs are stored as JSON `.lgm` documents.

## Tools

| Tool | Shortcut | Action |
|------|----------|--------|
| Select | `Esc` | Click or rubber-band to select; drag to move |
| Add Node | `N` | Click empty canvas |
| Add Branch | `B` | Click source node, then target |
| Add Two-Port | `P` | Click empty canvas to place a two-port (four nodes, two port branches) |
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
| Combine nodes | `M` (two selected nodes), or drop one node onto another; see below |
| Toggle two-port kind | `T` (transformer ↔ gyrator) |
| Select all | `Ctrl+A` |

Branches are directed (`from → to`). Multiple branches between the same node pair fan out perpendicular to the chord; each branch bows to the **left** of that direction.

### Combine nodes (`M`)

Branches on the removed node move to the kept node; a branch that joined the two nodes is dropped (it would become a self-loop) and comes back on undo.

- **Plain node → two-port across node** is always allowed (a note is shown if the energy domains differ; the two-port node's domain is kept).
- **Standalone reference (ground) → two-port reference** requires both nodes to be ground and in the same energy domain.
- **Cascade** — two-port nodes from *different* two-ports combine across-with-across (same energy domain) or reference-with-reference. A junction may be shared by any number of two-ports; every two-port using the removed node is re-pointed to the kept one.
- **Three-terminal** — the two reference nodes of *one* two-port may collapse into a shared reference. Any other merge inside one two-port, or across a port branch, is blocked.

### Delete and undo

Nodes, branches and two-ports carry a stable `serialId`. Undo commands and snapshots refer to items by that id (never by pointer or position), so undo/redo stays correct after items are deleted and re-created, and when nodes overlap.

Deleting a two-port captures a full **TwoPortSnapshot** (attachment nodes, both port branches and their direction, shared-ref flag, branch parameters) plus every external branch on the port nodes that disappear with it, so undo restores the complete topology. Purging a two-port preserves nodes still used by a **cascaded** sibling; such shared junctions stay owned by the surviving two-ports (`NodeItem::twoPorts()`).

## Docks and panels

- **Objects** — grouped tree of two-ports, nodes, and branches with icons; rename items inline; double-click to center the canvas.
- **Properties** — branch/two-port name, element constant, A/T/D type, active source flag, bow offset.
- **Analyze** — normal tree and state-space actions (see [analysis.md](analysis.md)).
- **State Space** — rendered state and output equations (`ẋ = Ax + Bu`, `y = Cx + Du` when outputs are selected) after a successful derivation.

## Document format (`.lgm`)

UTF-8 JSON, **version 4**. Every node and branch is stored once with an integer `id`; branches and two-ports refer to nodes (and two-ports to their port branches) by id. Positions are layout only, so two distinct nodes may share a position. Only version 4 is read; older files are rejected.

```json
{
  "format": "LGM",
  "version": 4,
  "settings": { "defaultSystemType": "electrical", "snapToGrid": true, "showGrid": true, "gridSpacing": 20,
                "nextNodeId": 7, "nextBranchId": 10, "nextSourceInputId": 2, "nextTwoPortId": 2 },
  "nodes": [
    { "id": 1, "name": "v₁", "pos": { "x": 20, "y": -100 }, "systemType": "electrical", "across": "V3" },
    { "id": 3, "name": "ref₁", "pos": { "x": 20, "y": 20 }, "systemType": "electrical", "ground": true }
  ],
  "branches": [
    { "id": 1, "from": 1, "to": 3, "name": "i1", "type": "T" },
    { "id": 6, "from": 6, "to": 1, "name": "i_L", "type": "T", "constant": "L" },
    { "id": 9, "from": 5, "to": 3, "name": "Vs1", "type": "A", "active": true, "sourceInputId": 1 }
  ],
  "twoPorts": [
    { "id": 1, "name": "Transformer 1", "kind": "transformer", "modulus": "TF",
      "v1": 1, "v2": 2, "g1": 3, "g2": 4, "leftBranch": 1, "rightBranch": 2 }
  ],
  "normalTrees": [ { "name": "Tree 1", "branches": [1, 5, 8, 9] } ],
  "activeNormalTreeIndex": 0,
  "outputVariables": ["T_J", "i_L"]
}
```

| Item | Fields |
|------|--------|
| node | `id`, `name`, `pos`, `systemType` (`mechanical`, `rotational`, `electrical`, `fluid`, `heat`); `ground: true` **or** `across` (symbol) |
| branch | `id`, `from`/`to` (node ids, direction of the branch), `name`, `type` (`A`, `T`, `D`); sources: `active: true` + `sourceInputId`; passive non-port branches: `constant`; optional `bow` (port bows are derived) |
| two-port | `id`, `name`, `kind` (`transformer`, `gyrator`), `modulus`, port nodes `v1`, `v2`, `g1`, `g2` (`g1 == g2` for a shared reference; cascaded two-ports share node ids), `leftBranch` (joins `v1`–`g1`) and `rightBranch` (joins `v2`–`g2`) — both also listed in `branches` |
| saved normal tree | `name`, `branches` (branch ids in the tree) |

Loading validates the whole document first (unique ids, every reference resolves, port branches join their two-port's nodes, valid enums and constants) and reports the first problem without touching the open graph. Saving sorts items by id, so the same graph always produces the same file; the examples are stored in exactly that form.

Use **File → Open / Save** (`Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`). Example graphs live in [Examples/](../Examples/README.md).

## Settings

**Settings** (or toolbar) controls:

- Default **domain** for new branches and node naming
- Grid snap, spacing, visibility
- Theme (system / light / dark)
- Customizable keyboard shortcuts

## Curve tuning

In `src/canvas/items/geometry.cpp`:

- `kLaneSpread` / `kFieldSpread` — gap between parallel branches (default `48`)
- `kFieldSingleBow` — arch of a lone branch as a fraction of chord length (default `0.08`)
- A branch's **bow** offsets its curve perpendicular to the chord (vertical branches: `+bow` bulges to `+x`), so level branches bow too.
- Two-port port branches bulge inwards; `kMinCouplerSpan` (default `48`) caps the bulge so closely cascaded couplers keep a usable width. The coupler is drawn along the line between the port-branch midpoints.

## Tests

`tests/canvas_undo_test.cpp` checks node/branch/two-port invariants, cascade and three-terminal merges, delete/undo of every example two-port and node, save/load round trips, and a seeded random edit/undo/redo fuzz:

```bash
cmake --preset <your-preset> -DLGM_BUILD_TESTS=ON
cmake --build build --target lgm_canvas_tests
ctest --test-dir build --output-on-failure
```

## Source files

| Path | Role |
|------|------|
| `canvas.h` | Scene, items, modes, analysis hooks |
| `items/` | Node, branch, two-port geometry and paint |
| `scene/scene_lifecycle.cpp` | Create/destroy nodes, branches, two-ports; preserves shared nodes when purging cascaded two-ports |
| `scene/scene_merge.cpp` | Combine nodes (including ground ↔ two-port ref), merge undo |
| `scene/scene_normal_tree.cpp` | Normal tree UI, saved trees, canvas twig IDs, state-space trigger (see [analysis.md](analysis.md)) |
| `canvas_scene_delete.cpp` | Delete capture/restore with `TwoPortSnapshot` |
| `document/` | `.lgm` load/save (`document_json.cpp`: schema, enum names, validation) |
| `undo/commands.cpp` | Undo stack commands |
| `canvas_scene_input.cpp` | Mouse/keyboard interaction |
| `canvas_view.cpp` | Zoom, pan, grid |
