# Quick Start

Linear Graph Modeling is a CAD-style editor for drawing **linear graphs**: nodes, directed branches, and two-port elements on a snapping grid.

## Canvas controls

| Action | Input |
|--------|--------|
| Pan view | Middle mouse drag, or **Space** + left drag |
| Zoom | Mouse wheel |
| Select | **Select** tool, click or rubber-band drag |
| Move node | Drag selected node |
| Add node | **Add Node** tool, click empty canvas |
| Connect nodes | **Add Branch** tool: click source, then target |
| Add two-port | **Add Two-Port** tool: click two attachment nodes |
| Delete selection | **Delete** or **Backspace** |
| Undo / Redo | **Ctrl+Z** / **Ctrl+Y** |

Branches bow to the left of the directed edge (`from → to`). Multiple branches between the same pair spread perpendicular to the chord.

## Typical workflow

1. Choose a **default domain** (mechanical, electrical, …) from the toolbar or **Settings**.
2. Draw the graph with nodes, branches, and two-ports.
3. Set element parameters in the **Properties** dock when a branch or two-port is selected.
4. Open **Analyze** to find or pick a **normal tree**, then compute **state space**.

## Analysis

- **Find Normal Tree** — automatic tree selection and highlight (green tree twigs, gray co-tree links).
- **Select Normal Tree** — click branches to build a manual tree, then apply.
- **Compute State Space** — derives state equations (`ẋ = Ax + Bu`) from the committed normal tree; check **output variables** first to also get `y = Cx + Du`.

See **Help → Guides → State-Space Derivation** for the in-app algorithm walkthrough. Source-level detail with file links: [docs/state_space_from_normal_tree.md](../docs/state_space_from_normal_tree.md) on GitHub.

## Files

Graphs are saved as `.lgm` JSON documents. Use **File → Open / Save** as usual.
