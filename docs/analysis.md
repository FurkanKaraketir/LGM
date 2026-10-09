# Analysis

After a graph is drawn and parameterized, LGM can select a **normal tree**, identify **state variables**, and derive **state equations** in MIT linear-graph form.

UI: **Analyze** dock and **State Space** dock (`src/ui/analyze_window.cpp`, `src/ui/mainwindow/`).

## Normal tree

A **normal tree** is a spanning tree chosen by MIT priority rules:

1. Across-variable sources (A-type actives) in the tree
2. As many **A-type** storage elements in the tree as possible (respecting two-port causality)
3. One branch per **transformer**; both or neither branch for each **gyrator**
4. **D-type** dissipators to fill the tree
5. Minimum **T-type** storage in the tree only if needed to complete it

**State variables** are then:

- Across variables on **A-type** storage branches **in the tree**
- Through variables on **T-type** storage branches **in the co-tree (links)**

### Automatic search

**Find Normal Tree** (`Ctrl+Shift+T`) runs `lg::computeNormalTree`. On success, tree twigs highlight **green**; co-tree links **gray**.

### Manual selection

**Select Normal Tree** (`Ctrl+Alt+T`) switches the canvas to tree-pick mode. Click branches to toggle membership, then **Apply** (`Enter` / `Return`). The selection is validated with `lg::validateManualNormalTree`.

**Save** is enabled when the canvas shows at least one branch marked in-tree — from an automatic **Find Normal Tree** highlight **or** from manual pick mode — not only when validation returns `Ok`. **Save** stores the current twig set by branch `serialId` (name via dialog).

**Apply saved tree** highlights the stored twigs and re-runs `validateManualNormalTree`. Invalid selections stay highlighted; the Analyze status line shows the branch count and validation message.

**Find Normal Tree**, **Find All Normal Trees**, showing a discovered tree, and **Apply saved tree** all exit manual selection mode first.

Saved trees can be named and recalled from the Analyze panel for comparison across formulations.

## State-space derivation

**Compute State Space** (`Ctrl+Shift+S`) requires a committed normal tree with `NormalTreeResult::status == Ok`. If branches are highlighted but validation failed, the error is the validation text (“The applied normal tree is not valid.” or the specific reason), not the generic “Find a valid normal tree first.”

Pipeline (`lg::computeStateSpace` in `src/model/state_space/state_space.cpp`):

1. Map state symbols to storage branches
2. Build **elemental** constitutive equations (node-across form)
3. Identify **inputs** (active A in tree, active T in co-tree)
4. **Continuity** — flow replacements from tree cut-sets and port-span junctions; **cascaded transformers** (shared ref, different spans) use KCL at the reference node; **D-type** co-tree twigs on a port span may be solved from the junction before the port flow is recorded
5. **Compatibility** — active A-type source bindings (`V_node = u`)
6. **Two-port** across constraints (flows stay in elementals + continuity)
7. Solve for `state_dot` from storage elementals
8. Eliminate remaining branch/node symbols (guards block recycling a state's own `_dot` and folding foreign `x_dot` when the target state's elemental already defines its dot); coupled blocks use Gaussian elimination per Geddes, Czapor & Labahn (1992), Ch. 9 §9.2
9. Emit state equations and LaTeX `ẋ = A x + B u [+ E u̇]`
10. For each **output variable** selected in the Analyze panel, emit scalar output equations and LaTeX `y = C x + D u [+ F u̇]` (primary **state** symbols seed directly, giving identity **C** rows when a state is selected as output)

Before step 10, check observables under **Output variables (C and D matrices)** in the Analyze dock (**Select All** / **Clear All** shortcuts are available). Recompute after changing the selection.

Results appear in the **State Space** dock: elemental, continuity, compatibility, state equations, state matrix form, and (when outputs are selected) output equations plus the **C** / **D** matrix form (JKQtMathText).

### MATLAB® export

After a successful **Compute State Space**, use **Export MATLAB® Script** in the Analyze dock to save `A`, `B`, `C`, `D`, and optional `E` / `F` matrices as a `.m` script. The file is intended for use with [MATLAB®](https://www.mathworks.com/products/matlab.html) from The MathWorks, Inc.

MATLAB and Simulink are registered trademarks of The MathWorks, Inc. See [mathworks.com/trademarks](https://www.mathworks.com/company/trust-center/trademarks.html) for additional trademarks.

### Status codes

| Result | Meaning |
|--------|---------|
| `Ok` | Equations derived |
| `NeedNormalTree` | No normal tree committed, or the highlighted/applied tree failed validation |
| `NotConnected` / `ForcedCycle` / `Incomplete` / `TwoPortConstraint` | Normal-tree failure |
| `Unsupported` / `SymbolicError` / `GraphError` | Derivation could not complete |

### Diagnostics

Run `.\build\LGM.exe` (or `LGM` on macOS/Linux) from a terminal to see debug output. All Qt log lines — including `[state_space] …` tags during derivation — are mirrored to **stderr** as well as the in-app log panel. This is especially useful on Windows where the GUI has no built-in console.

## Algorithm reference

Also available in the repository (with source links): [docs/state_space_from_normal_tree.md](state_space_from_normal_tree.md)

## Theory background

- `LinearGrpahModelingBasics.txt` — normal trees and state-equation procedure
- `TwoPortsBasics.txt` — two-port trees and multi-domain graphs
- Altun et al. (2002) — graph assembly (normal tree, elementals, continuity, compatibility); see **Help → About**
- Geddes, Czapor & Labahn (1992), *Algorithms for Computer Algebra*, Ch. 9 §9.2 — symbolic elimination and Gaussian elimination on coupled linear subsystems; see **Help → About**

## Example workflow

1. Open [Examples/Motor.lgm](../Examples/Motor.lgm) — PM DC motor with electrical/mechanical domains.
2. **Find Normal Tree** — expect states such as `OmegaJ`, `i_L` and input `Vs1`.
3. **Compute State Space** — coupled motor dynamics through `Ka`, `J`, `L`, `R`, `B`; optionally check `OmegaJ`, `i_L`, or other outputs in Analyze to review **C** and **D** matrices.

Other samples: [Examples/README.md](../Examples/README.md).
