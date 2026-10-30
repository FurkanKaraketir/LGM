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

**Find All Normal Trees** (`Ctrl+Shift+T`, in the **Analysis** menu and the Analyze dock) runs `lg::enumerateNormalTrees`: a Kruskal seed tree for every feasible two-port port assignment, then equivalent-branch replacement. Trees are ranked by `TreeScore` and only those that tie the best one are listed under **Valid Normal Trees** — a tree with more A-type (or fewer T-type) twigs hides worse alternatives. Tree 1 is highlighted: twigs **green**, co-tree links **gray**; double-click another listed tree to show it instead.

### Manual selection

**Select Normal Tree** (`Ctrl+Alt+T`, or **Select Manually** in the Analyze dock) switches the canvas to tree-pick mode. Click branches to toggle membership, then **Apply Selection** (`Enter` / `Return`). The selection is validated with `lg::validateManualNormalTree`, which also rejects an active T-type source in the tree and a tree that is not normal for its own two-port assignment (fewer passive A-type or more passive T-type twigs than the greedy tree).

**Save Current** is enabled when the canvas shows at least one branch marked in-tree — from a **Find All Normal Trees** highlight **or** from manual pick mode — not only when validation returns `Ok`. It stores the current twig set by branch `serialId` (name via dialog) under **Saved Normal Trees**.

**Apply saved tree** highlights the stored twigs and re-runs `validateManualNormalTree`. Invalid selections stay highlighted; the Analyze status line shows the branch count and validation message.

**Find All Normal Trees**, showing a discovered tree, and **Apply saved tree** all exit manual selection mode first.

Saved trees can be named and recalled from the Analyze panel for comparison across formulations.

## State-space derivation

**Compute State Space** (`Ctrl+Shift+E`) requires a committed normal tree with `NormalTreeResult::status == Ok`. If branches are highlighted but validation failed, the error is the validation text (“The applied normal tree is not valid.” or the specific reason), not the generic “Find a valid normal tree first.”

Pipeline (`lg::computeStateSpace` in `src/model/state_space/state_space.cpp`):

1. Map each state to its storage branch (the normal tree records it)
2. Build **elemental** constitutive equations (node-across form) for every passive branch, plus the **two-port** laws
3. Identify **inputs** (active A in tree, active T in co-tree)
4. **Continuity** (fundamental cut-set of each twig) and **compatibility** (`u = v_from − v_to` for each active A-type source) — shown in the results, not used to filter the solve
5. Assemble the full linear graph system: KCL at every node (reference nodes and port branches included), source equations, D-type and two-port laws, state definitions, the time derivative of each of these, and the A/T elementals. The differentiated equations couple **dependent storage** (e.g. inertias tied through transformers) and give the input-derivative (**E** / **F**) terms
6. Solve for every `x_dot` and every selected output with Gaussian elimination (`solveLinearTargets`, Geddes, Czapor & Labahn (1992), Ch. 9 §9.2) using a rational zero test. An inconsistent system (dependent sources or states) or an undetermined target stops with an error instead of a wrong result
7. Emit state equations and LaTeX `ẋ = A x + B u [+ E u̇]`
8. For each **output variable** selected in the Analyze panel, emit scalar output equations and LaTeX `y = C x + D u [+ F u̇]`; outputs are targets of the same solve, so **C** / **D** / **F** agree with **A** / **B** / **E** (a selected state gives an identity **C** row)

Phase-by-phase detail with Motor.lgm logs: [state_space_from_normal_tree.md](state_space_from_normal_tree.md).

Before computing, check observables under **Output variables (C and D matrices)** in the Analyze dock (**Select All** / **Clear All** shortcuts are available). Recompute after changing the selection.

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

- [LinearGrpahModelingBasics.txt](LinearGrpahModelingBasics.txt) — normal trees and state-equation procedure
- [TwoPortsBasics.txt](TwoPortsBasics.txt) — two-port trees and multi-domain graphs
- Altun et al. (2002) — graph assembly (normal tree, elementals, continuity, compatibility); see **Help → About**
- Geddes, Czapor & Labahn (1992), *Algorithms for Computer Algebra*, Ch. 9 §9.2 — symbolic elimination and Gaussian elimination on coupled linear subsystems; see **Help → About**

## Example workflow

1. Open [Examples/Motor.lgm](../Examples/Motor.lgm) — PM DC motor with electrical/mechanical domains.
2. **Find All Normal Trees** — expect states such as `OmegaJ`, `i_L` and input `Vs1`.
3. **Compute State Space** — coupled motor dynamics through `Ka`, `J`, `L`, `R`, `B`; optionally check `OmegaJ`, `i_L`, or other outputs in Analyze to review **C** and **D** matrices.

Other samples: [Examples/README.md](../Examples/README.md).
