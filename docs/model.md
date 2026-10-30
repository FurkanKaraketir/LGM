# Graph model

The symbolic layer in `src/model/` sits behind the canvas. It classifies branches, builds constitutive (**elemental**) equations, and feeds the analysis pipeline.

## Energy domains

`SystemType` selects variable naming and equation conventions:

| Domain | Across variable | Through variable |
|--------|-----------------|------------------|
| Mechanical (translational) | velocity `v` | force `F` |
| Mechanical (rotational) | angular velocity `Omega` | torque `T` |
| Electrical | voltage `V` | current `i` |
| Fluid | pressure `P` | volume flow `Q` |
| Heat | temperature `T` | heat flow `q` |

Set the default domain from the toolbar or **Settings**. New nodes get domain-appropriate reference labels; branch effort expressions use node-across differences (e.g. `V1 - V2`, `OmegaJ`).

## Branch types (MIT linear graph)

Passive elements are **A**, **T**, or **D** (`BranchType`):

| Type | Role | Typical elements | State when… |
|------|------|------------------|-------------|
| **A** | Inertia, capacitance | `J`, `C`, `L` (fluid inertance) | **In tree** → across variable is a state |
| **T** | Compliance, inductance | `K`, `L` (inductor), fluid capacitance | **In co-tree (link)** → through variable is a state |
| **D** | Resistance, damping | `R`, `B` | Algebraic only; never a state |

**Active sources** — mark a branch **active** in Properties:

- A-type in the **tree** → effort/across **input**
- T-type in the **co-tree** → flow/through **input**

Element constants (`R`, `L`, `J`, …) can **infer** type when the branch is still default A-type (e.g. `R` → D, `L` → T).

**Plain identifiers** (`R`, `L`, `I`, `M`, `R_leak`, …) parse as SymEngine **symbols**, not built-in math constants — so `I` is inertia, not the imaginary unit, and names like `E` or `pi` are not silently reinterpreted (`parseElementConstant` in `constants.cpp`).

Elemental equations are built from `elemental_equation/` (`branchNodeAcrossExpr` in `symbols.cpp`, `elementalEquationText` in `constants.cpp`).

## Two-port elements

`TwoPortKind`:

- **Transformer** — relates across-to-across and through-to-through (e.g. gear train, DC motor `1/Ka`).
- **Gyrator** — couples across on one port to through on the other (e.g. hydraulic ram).

Each two-port has two port branches attached to graph nodes, a modulus constant, and transformer/gyrator-specific constitutive laws. Only **one** transformer port branch may lie on the normal tree; gyrator causality requires **both** port branches in the tree or **both** in the co-tree.

**Cascaded and stacked two-ports** — port branches are ordinary graph branches, so KCL at every node (shared reference nodes included) covers them without special cases; see Phase E in [state_space_from_normal_tree.md](state_space_from_normal_tree.md).

**Same-span conflict** in normal-tree scoring applies when **both port branches of a transformer** are in the tree alongside a parallel user branch on that span — gyrators are not penalized by this rule (`scoreTree` in `normal_tree/build.cpp`).

See [TwoPortsBasics.txt](TwoPortsBasics.txt) for theory.

## Nodes

Nodes are junctions (reference nodes for each energy domain). Multiple branches and two-port attachments meet at a node. **Combine nodes** (`M`) merges coincident nodes for cleaner graphs.

## Key source files

| Path | Role |
|------|------|
| `elemental_equation.h` | Public API; symbols, types, elemental laws, port-span helpers for tree scoring |
| `elemental_equation/` | `constants`, `naming`, `symbols`, `topology` (port-span helpers, two-port law text) |
| `normal_tree.h` | Normal-tree result types and public API |
| `normal_tree/` | `build` (search), `enumerate`, `normal_tree` (API glue) |
| `state_space.h` | Result types (`matrixForm`, `outputMatrixForm`, …) and `computeStateSpace` entry |
| `state_space/` | `state_space` (orchestrator), `context` (shared derivation state: states, elementals, outputs), `constraints` (elementals, display equations), `states` (linear system and solve), `matrix` (A/B/E and C/D/F) |
| `state_space_graph.cpp` | Graph walks (cut-sets), output variable choices |
| `state_space_sym.cpp` | SymEngine linear solve `solveLinearTargets` (Geddes et al., 1992, Ch. 9 §9.2) |
| `state_space_latex.cpp` | LaTeX matrix form |
| `state_space_matlab.cpp` | MATLAB® `.m` script export |

Symbolic math uses [SymEngine](https://github.com/symengine/symengine) with Boost.Multiprecision integers.
