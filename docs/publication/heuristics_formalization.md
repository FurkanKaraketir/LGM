# LGM heuristic algorithms (formalization)

Formal descriptions of implementation-specific procedures in LGM that extend the textbook MIT / Altun (2002) linear-graph state-space pipeline. These are **engineering algorithms**, not published theorems. Source references point into the codebase.

**Notation:** \(G=(N,B)\) graph with nodes \(N\), branches \(B\). Tree \(T \subset B\), co-tree \(L=B\setminus T\). Across variables on nodes, through variables on branches.

Golden CLI snapshots: [`benchmarks/golden/`](benchmarks/golden/) (regenerate with [`benchmarks/generate.ps1`](benchmarks/generate.ps1)).

---

## 1. TreeScore lexicographic ranking

**Purpose:** When multiple minimum-weight normal trees exist, pick the tree that best supports state-space derivation (more A-type storage in tree, fewer topological conflicts).

**Implementation:** [`src/model/normal_tree/build.cpp`](../../src/model/normal_tree/build.cpp) — `scoreTree`, `TreeScore::betterThan`

### Score vector

For candidate tree \(T\), compute:

| Field | Definition |
|-------|------------|
| `sameSpanTreeConflicts` | Count of invalid same-span placements (see below) |
| `aPassive` | Passive A-type branches in \(T\) |
| `dPassive` | Passive D-type branches in \(T\) |
| `portInTree` | Two-port internal port bonds in \(T\) |
| `externalOnPortSpanInTree` | User branches parallel to a port span that lie in \(T\) |
| `tPassive` | Passive T-type branches in \(T\) |

**Conflict rules** (increment `sameSpanTreeConflicts`):

1. Transformer: both port bonds of the same transformer in \(T\).
2. For each port bond \(p \in T\): if a user branch is parallel to \(p\) on the same node span and also in \(T\), count one conflict.

Trees with `sameSpanTreeConflicts > 0` are rejected in enumeration ([`acceptEnumeratedTree`](../../src/model/normal_tree/enumerate.cpp)).

### Comparison (lexicographic minimization / maximization)

Tree \(A\) is **better than** tree \(B\) iff, in order:

1. `sameSpanTreeConflicts` **smaller**
2. `aPassive` **larger**
3. `dPassive` **larger**
4. `portInTree` **larger**
5. `externalOnPortSpanInTree` **smaller**
6. `tPassive` **smaller**

### Pseudocode

```
function SCORE_TREE(T, branches, twoPorts) -> TreeScore:
    s <- empty TreeScore
    for b in T where b passive:
        increment s by type of b (A/D/T/port/external flags)
    for each transformer with ports (left, right):
        if left in T and right in T: s.conflicts++
        for port in {left, right} where port in T:
            ext <- externalBranchParallelToPort(port)
            if ext in T: s.conflicts++
    return s

function BETTER_THAN(a, b) -> bool:
    return lexicographic_compare(a, b) as in table above
```

### Worked trace ([`MultiNormalTree.lgm`](../../Examples/MultiNormalTree.lgm))

Topology: grounded `Vs1` on \(G\)–\(V_1\); resistors `R1` (\(V_1\)–\(V_2\)), `R2` (\(V_1\)–\(V_3\)); capacitor `C` (\(V_2\)–\(V_3\)).

Two valid spanning trees (both pass `validateManualNormalTree`):

| Tree twigs | Co-tree | MST weight | TreeScore \((c, a, d)\) |
|------------|---------|------------|-------------------------|
| `Vs1`, `R1`, `C` | `R2` | \(1+3+2=6\) | \((0, 1, 1)\) — **preferred** |
| `Vs1`, `R1`, `R2` | `C` | \(1+3+3=7\) | \((0, 0, 2)\) |

`computeNormalTree` picks the first row (lower weight and higher `aPassive`). CLI golden state: `C_across` ([`benchmarks/golden/MultiNormalTree.txt`](benchmarks/golden/MultiNormalTree.txt)).

**Counterexample (rejected):** placing an active source and a parallel passive branch on the same span in \(T\) increments `sameSpanTreeConflicts`; such trees fail `acceptEnumeratedTree` before scoring.

---

## 2. Normal tree enumeration

**Purpose:** List valid normal trees (for user comparison and `computeNormalTree` best pick).

**Implementation:** [`src/model/normal_tree/enumerate.cpp`](../../src/model/normal_tree/enumerate.cpp)

### Inputs

- Graph \(G\), two-port set with transformer/gyrator constraints
- Branch weights: active A=1, passive A=2, D=3, passive T=4, active T=5

### Algorithm

```
function ENUMERATE_NORMAL_TREES(G):
    assignments <- all 2^(#transformers) port choices x 2^(#gyrators) in/out choices
    trees <- []
    for each assignment:
        seed <- KRUSKAL_MST(G, forced_in, forbidden_from_assignment)
        if seed invalid: continue
        BFS queue <- [seed]
        while queue not empty and |trees| < MAX (200):
            T <- pop(queue)
            if acceptEnumeratedTree(T): append T to trees
            for each co-tree link l with weight w:
                for each tree twig t on fundamental loop of l with weight w:
                    T' <- (T \ {t}) ∪ {l}
                    enqueue T' if novel
    sort trees by TreeScore (best first)
    return trees
```

`acceptEnumeratedTree` requires `sameSpanTreeConflicts = 0` and `validateManualNormalTree = Ok`.

### Worked trace ([`MultiNormalTree.lgm`](../../Examples/MultiNormalTree.lgm))

- **Bound:** `MAX = 200` trees per port assignment (ponytail: coursework-scale graphs only).
- **Completeness:** scoped to the finite graph of **equal-weight swaps** on fundamental loops; not a claim about all spanning trees of \(G\).
- **Use:** **Analyze → Find All Normal Trees** lists both rows in §1; user can compare state equations for alternate formulations.

[`TransformerCascade.lgm`](../../Examples/TransformerCascade.lgm) adds transformer port-assignment enumeration (\(2^3\) transformer choices before BFS swaps).

---

## 3. Cascaded transformer KCL (port-span junction)

**Purpose:** At a transformer reference (ground) node shared by cascaded two-ports, apply KCL for incident branches that are not simple same-span parallels.

**Implementation:** [`src/model/elemental_equation/topology.cpp`](../../src/model/elemental_equation/topology.cpp) — `signedSpanJunctionFlowSum`

### Pseudocode

```
function SIGNED_SPAN_JUNCTION_FLOW_SUM(port, branches, excludePort):
    ground <- ground node of port
    sum <- 0
    for branch b in branches:
        if b is parallel on port span:
            sum += signedParallelFlow(b, port)
        else if ground and b incident on ground:
            sum += signedFlowIntoGround(b, ground)   // cascade case
    return sum
```

**Continuity cut omission:** [`omitFromContinuityCut`](../../src/model/elemental_equation/topology.cpp) skips duplicate port-bond counting when stacked transformers share one span but each port bond is an independent flow.

### Correctness sketch

At shared reference node \(g\):

1. **Same-span parallels** (user \(R\) beside port bond): standard junction — flows sum to zero via `signedParallelFlowExpr`.
2. **Cascade case** (port bonds on **different** spans, common \(g\) only): treat \(g\) as a 0-junction in bond-graph terms — all flows **incident on** \(g\) (not just same-span) enter KCL.
3. Port flow is then \(f_p = -\sum_{b \neq p} \text{signed}(f_b)\) ([`state_space_constraints.cpp`](../../src/model/state_space/state_space_constraints.cpp) continuity phase).

### Worked trace ([`TransformerCascade.lgm`](../../Examples/TransformerCascade.lgm))

Three cascaded transformers share mechanical reference `ref₂`. Co-tree port bonds on different spans contribute through `signedSpanJunctionFlowSum` rather than being treated as parallel on one span. Golden states: `Omega2`, `f_K` ([`benchmarks/golden/TransformerCascade.txt`](benchmarks/golden/TransformerCascade.txt)). Continuity log shows coupled port flows (e.g. `T3 = -(T2 + T_I_1)`).

[`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm) stacks three transformers across electrical, fluid, and mechanical domains — same KCL rule at shared refs.

---

## 4. Mass-in-tree compliance reflection

**Purpose:** When mass (A-type, constant `M`) is in the tree and a T-type compliance is in the co-tree on a **different** across node, reflect the compliance through-force into the mass branch flow via the transformer modulus product on the path between nodes.

**Implementation:** [`src/model/state_space/state_space_reflect.cpp`](../../src/model/state_space/state_space_reflect.cpp) — `ssReflectAndBind`

### Preconditions

- Tree contains passive A branch with constant starting with `M`
- Co-tree contains passive T storage state on a **different** across node
- `transformerModulusProductBetween(massNode, complianceNode)` is defined
- Skip if mass and compliance share the same across node (parallel B/K/M handled by port-span junction)

### Pseudocode

```
function REFLECT_MASS_COMPLIANCE(ctx):
    mass <- first M-type A branch in tree
    compliance <- first T-type storage state in co-tree
    if acrossNode(mass) == acrossNode(compliance): return
    TF_prod <- transformerModulusProductBetween(nodes)
    if TF_prod undefined: return
    inputSum <- sum of active T inputs at mass node, else first system input
    reflected <- compliance.through / TF_prod
    recordReplacement(mass.through, inputSum - reflected)
```

### Derivation chain

1. Co-tree compliance \(q_c\) has constitutive law tying its through variable to across derivative.
2. Continuity at the mass node would leave an unresolved port/transformer flow.
3. Multiply compliance force by \(\prod TF\) along the transformer chain between mass and compliance across nodes — power-conjugate scaling.
4. Substitute into mass tree-branch flow replacement before \(\dot{x}\) assembly.

### Worked trace ([`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm))

States: `Q_L_f`, `i_L`, `Vm` (mass `f_M`). Fluid compliance `Q_C_f` in co-tree reflects into mechanical mass branch via `K_a`, `K_b`. Golden coupling: `Vm_dot` contains `Q_L_f` term; `i_L_dot` contains `Q_L_f/(L*K_a*K_b)` ([`benchmarks/golden/PumpAccumulator.txt`](benchmarks/golden/PumpAccumulator.txt)).

---

## 5. Synthetic across state binding

**Purpose:** When a storage branch uses a tautology across symbol (e.g. `C5_across = V1 - V2`), bind the free node across derivative to inputs and the synthetic state.

**Implementation:** [`src/model/state_space/state_space_reflect.cpp`](../../src/model/state_space/state_space_reflect.cpp), [`src/model/elemental_equation/symbols.cpp`](../../src/model/elemental_equation/symbols.cpp), [`src/model/state_space_sym.cpp`](../../src/model/state_space_sym.cpp) — `rewriteForSyntheticAcrossStateDot`

### Pseudocode

```
for each state with symbol == branchTautologyAcross(branch):
    parse "V_hi - V_lo" from across text
    bind V_lo_dot = u_hi_dot - state_dot   // or symmetric variant
```

### State equivalence

Capacitors `C4`, `C5` on the same span share one independent across variable. Choosing `C5_across = V1 - V2` as state is equivalent to eliminating one node voltage and keeping the difference coordinate — a node-reduction choice, not a distinct physics model.

### Worked trace ([`Electrical.lgm`](../../Examples/Electrical.lgm))

Golden state vector: `C5_across`, `i_L7`. Binding gives `C5_across_dot` coupled to `Vs1_dot` and `Is2` ([`benchmarks/golden/Electrical.txt`](benchmarks/golden/Electrical.txt)). `rewriteForSyntheticAcrossStateDot` rewrites \(\dot{u}\) dependencies in the final state equations.

---

## 6. State-dot coupling guards

**Purpose:** Prevent invalid symbolic elimination that introduces algebraic loops in \(\dot{x}\).

**Implementation:** [`src/model/state_space/state_space_states.cpp`](../../src/model/state_space/state_space_states.cpp)

### Rules (substitution filter)

For elimination candidate solving symbol \(q\) into target state derivative \(\dot{x}_i\):

1. **Own-dot recycle:** reject if solved form contains \(\dot{q} = f(\dot{q})\) when \(q\) is the state's own through/across dot.
2. **Foreign state dots:** if the state's elemental already defines \(\dot{x}_i\) via its branch flow, reject substitutions that introduce other \(\dot{x}_j\).
3. **Input preservation:** do not drop existing \(u\) or \(\dot{u}\) coefficients.

### Pseudocode

```
function ACCEPT_SUBSTITUTION(target, expr_before, expr_after, solved):
    if recycles own Q_dot in solved: return false
    if target state's elemental defines dot via own flow:
        if expr_after depends on foreign x_dot: return false
    if input or input_dot coefficient lost: return false
    return true
```

### Worked trace

- [`Electrical.lgm`](../../Examples/Electrical.lgm): guards block eliminations that would inject foreign \(\dot{C5}_{across}\) into `i_L7_dot` after elemental `i_L7` is fixed.
- [`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm): multi-domain substitutions filtered before matrix assembly.

---

## 7. Coupled algebraic subsystem solve

**Purpose:** After forming \(\dot{x}\) candidates, resolve mutually coupled linear relations among \(\dot{x}\) (and algebraic nodes) symbolically.

**Theory reference:** Standard symbolic elimination on linear systems — iterative substitution (`eliminateSymbolsInto`, `solveLinearFor`) plus **Gaussian elimination with back substitution** on coupled blocks per Geddes, Czapor & Labahn (1992), *Algorithms for Computer Algebra*, **Ch. 9 §9.2 *Linear Equations and Gaussian Elimination*** ([literature_review.md](literature_review.md) §1.4). Altun (2002) covers graph assembly; this step is conventional computer algebra.

**Implementation:** [`src/model/state_space_sym.cpp`](../../src/model/state_space_sym.cpp) — `resolveStateDotCoupling`, `solveLinearSubsystem`

### Pseudocode

```
function RESOLVE_STATE_DOT_COUPLING(states, equations):
    for pass in 1..4:
        decouple independent states
        for each coupled block B:
            solveLinearSubsystem(B) via symbolic Gaussian elimination
    return updated state_dot expressions
```

Bounded to small order (typical coursework graphs \(n \leq 5\)).

### Worked trace

[`Electrical.lgm`](../../Examples/Electrical.lgm): coupled `C5_across_dot` and `i_L7_dot` with \(\dot{u}\) terms resolved in ≤4 passes. [`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm): 3-state block after reflection.

---

## 8. Floating across sources

**Purpose:** Apply compatibility for an active A-type source whose **neither** endpoint is ground.

**Implementation:** [`src/model/state_space/state_space_constraints.cpp`](../../src/model/state_space/state_space_constraints.cpp) — compatibility phase (~L223).

### Precondition

- Branch is active, type A, in normal tree \(T\).
- Endpoints `from`, `to` both non-ground.

### Rule

\[
v_{\text{from}} = u + v_{\text{to}}
\]

where \(u\) is the source input symbol (`Vs1`, `Ps1`, …). Equivalently \(u = v_{\text{from}} - v_{\text{to}}\).

**Grounded case (Altun/MIT default):** if `to` is ground, \(v_{\text{from}} = u\); if `from` is ground, \(v_{\text{to}} = u\).

### Worked trace ([`FloatingSource.lgm`](../../Examples/FloatingSource.lgm))

Topology: `Vs1` between `Va` and `Vb`; `R` from `Va` to ground; `L` from `Vb` to ground.

Compatibility: `Va = Vs1 + Vb`.

Golden: single state `i_L`, `i_L_dot = -Vs1(t)/L - R*i_L/L` ([`benchmarks/golden/FloatingSource.txt`](benchmarks/golden/FloatingSource.txt)).

**Secondary:** [`Gyrator.lgm`](../../Examples/Gyrator.lgm) `Ps1` is a floating pressure source between fluid nodes; compatibility sets `P1 = Ps1 + P6` before gyrator binding.

---

## 9. Example corpus matrix

| Example | Heuristics exercised | States (golden) | Inputs | Key artifacts |
|---------|---------------------|-----------------|--------|---------------|
| [`Motor.lgm`](../../Examples/Motor.lgm) | Baseline pipeline, TreeScore | `OmegaJ`, `i_L` | `Vs1` | Standard Altun path; see [`benchmarks/golden/Motor.txt`](benchmarks/golden/Motor.txt) |
| [`Electrical.lgm`](../../Examples/Electrical.lgm) | Synthetic across, coupling guards, algebraic solve | `C5_across`, `i_L7` | `Is2`, `Vs1` | `C5_across_dot` with \(\dot{u}\) |
| [`Gyrator.lgm`](../../Examples/Gyrator.lgm) | Gyrator port constraint, floating source | `f_k`, `v1` | `Ps1` | Both gyrator ports in tree |
| [`TransformerCascade.lgm`](../../Examples/TransformerCascade.lgm) | Cascade KCL, enumeration | `Omega2`, `f_K` | `Fs1` | Port-span junction sums |
| [`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm) | Cascade KCL, mass reflect, guards, solve | `Q_L_f`, `i_L`, `Vm` | `Fs2`, `Vs1` | Cross-domain TF chain |
| [`FloatingSource.lgm`](../../Examples/FloatingSource.lgm) | Floating across source | `i_L` | `Vs1` | `Va = Vs1 + Vb` |
| [`MultiNormalTree.lgm`](../../Examples/MultiNormalTree.lgm) | TreeScore, enumeration | `C_across` | `Vs1` | ≥2 valid trees, tie-break §1 |

Regenerate all golden files: [`benchmarks/generate.ps1`](benchmarks/generate.ps1).

---

## 10. Gap analysis vs Altun (2002)

Altun, Balkan & Platin (2002) and MIT 2.151 give the **core** normal-tree procedure: elemental equations, cut-set continuity, compatibility, state assembly. LGM implements that pipeline and adds the heuristics below where the published algorithm is silent or course notes are brief.

| Topic | Altun (2002) / MIT baseline | LGM extension | Evidence |
|-------|----------------------------|---------------|----------|
| Normal tree + cut/tie assembly | Core algorithm | Reused as-is | [`Motor.lgm`](../../Examples/Motor.lgm), [`docs/state_space_from_normal_tree.md`](../state_space_from_normal_tree.md) |
| Multi-tree tie-break | Not specified | TreeScore lex order (§1) | [`MultiNormalTree.lgm`](../../Examples/MultiNormalTree.lgm) |
| Normal-tree enumeration | Not in scope | BFS equal-weight swap (§2) | UI **Find All Normal Trees** |
| Cascaded transformers (shared ref, different spans) | Implicit / brief in notes | `signedSpanJunctionFlowSum`, `omitFromContinuityCut` (§3) | [`TransformerCascade.lgm`](../../Examples/TransformerCascade.lgm), [`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm) |
| Floating A-sources | Grounded-source compatibility only | \(v_{\text{from}} = u + v_{\text{to}}\) (§8) | [`FloatingSource.lgm`](../../Examples/FloatingSource.lgm) |
| Synthetic across states | Not discussed | Tautology binding + `rewriteForSyntheticAcrossStateDot` (§5) | [`Electrical.lgm`](../../Examples/Electrical.lgm) |
| Mass-in-tree + remote compliance | Not discussed | `ssReflectAndBind` (§4) | [`PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm) |
| Symbolic \(\dot{x}\) loop prevention | Not discussed | Coupling guards + `resolveStateDotCoupling` (§6–7) | [`Electrical.lgm`](../../Examples/Electrical.lgm) |
| Symbolic elimination of constraints | Implicit after assembly | `solveLinearFor` + substitution; coupled blocks via Ch. 9 §9.2 Gaussian elimination (Geddes et al., 1992, [literature_review.md](literature_review.md) §1.4) | Phase H in [`state_space_from_normal_tree.md`](../state_space_from_normal_tree.md) |

**What remains standard:** passive constitutive laws, transformer/gyrator elemental equations, active-source tree placement, and the overall \(\dot{x}=Ax+Bu\), \(y=Cx+Du\) assembly pattern — see [originality_assessment.md](originality_assessment.md).

**Cross-formalism comparison** (bond graph, Modelica) on identical physics: [cross_tool_comparison.md](cross_tool_comparison.md).

---

## Heuristic coverage map

| Heuristic | Practical role | Formalization | Section |
|-----------|----------------|---------------|---------|
| TreeScore | Tie-breaking for multi-tree graphs | **Done** — preference rules + MultiNormalTree counterexample | §1 |
| Enumeration | User feature “Find All Normal Trees” | **Partial** — bounded BFS on equal-weight swap graph | §2 |
| Cascade KCL | Enables multi-domain examples | **Done** — KCL sketch at shared ref | §3 |
| Mass reflect | PumpAccumulator works | **Done** — continuity + TF chain derivation | §4 |
| Synthetic across | Electrical example | **Done** — state equivalence to node reduction | §5 |
| Coupling guards | Stable symbolic output | **Partial** — engineering rules, no termination proof | §6 |
| Algebraic solve | Coupled \(\dot{x}\) blocks | **Partial** — bounded Gaussian passes | §7 |
| Floating source | Floating voltage/pressure | **Done** — compatibility rule + examples | §8 |
