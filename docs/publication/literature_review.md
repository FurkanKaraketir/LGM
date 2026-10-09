# Literature review: linear graphs, bond graphs, and state-space tools

Comparative background for LGM. LGM follows the **MIT linear graph** formulation (across/through on nodes and branches), not standard **bond-graph** notation — but the physical modeling goals overlap.

---

## 1. Theoretical foundations

### 1.1 Linear graph theory (MIT)

| Reference | Contribution | Relation to LGM |
|-----------|--------------|-----------------|
| MIT 2.151 course notes ([`LinearGrpahModelingBasics.txt`](../LinearGrpahModelingBasics.txt), [`TwoPortsBasics.txt`](../TwoPortsBasics.txt)) | Normal trees, elemental/continuity/compatibility equations, state-equation procedure | **Primary pedagogy** |
| Koenig, Tokad, Kesavan & Hedges, *Analysis of Discrete Physical Systems* (1967) | Systematic linear graph methods | Cited in MIT notes |
| Shearer, Murphy & Richardson, *Introduction to System Dynamics* (1967) | Classic system dynamics | Background |
| Durfee, Armstrong & Donath (1991), IEEE Control Systems | Linear graph modeling overview | Survey context |

### 1.2 Bond graphs (Paynter school)

| Reference | Contribution | Relation to LGM |
|-----------|--------------|-----------------|
| Paynter, *Analysis and Design of Engineering Systems* (1961) | Bond graphs, effort/flow, junctions | Conceptual parallel; different drawing rules |
| Borutzky, *Bond Graph Methodology* (2010) | Modern bond-graph reference | Tool comparison baseline |
| 20-sim documentation | Causal bond graphs → state space | Closest commercial analog |

**Key distinction:** Bond graphs assign causality to bonds; MIT linear graphs use **normal trees** to select mixed across/through states. LGM implements the latter.

### 1.3 State variable extraction (direct antecedent)

| Reference | Contribution | Relation to LGM |
|-----------|--------------|-----------------|
| **Altun, K.; Balkan, R. T.; & Platin, B. E. (2002)**. *Extraction of state variable representations of dynamic systems employing linear graph theory.* [doi:10.13140/RG.2.2.29940.96647](https://doi.org/10.13140/RG.2.2.29940.96647) | Algorithmic normal-tree procedure for state equations | **Primary academic citation in LGM README** |
| Altun, Platin & Balkan (2003), IMECE03 | Systematic state equation derivation | Follow-on conference paper |
| Altun, M.S. thesis, METU (2002), *Computer aided analysis of dynamic systems employing graph theory* | Thesis-length treatment | Historical |

LGM is best described as a **modern open-source computer implementation** of the Altun/MIT procedure with symbolic algebra (SymEngine) and extensions for cascaded multi-port graphs.

### 1.4 Symbolic elimination (computer algebra)

After Altun/MIT assembly (elemental, continuity, compatibility), LGM reduces branch flows, node across variables, and coupled \(\dot{x}\) blocks to expressions in \(\{x, u, \dot{u}\}\) by **standard symbolic elimination** per Geddes, Czapor & Labahn (1992), **Ch. 9 §9.2** *Linear Equations and Gaussian Elimination*: solve one equation for one unknown (`solveLinearFor`), substitute into the remaining targets, repeat to fixpoint; coupled leftovers are handled by Gaussian elimination with back substitution on the linear subsystem ([`solveLinearSubsystem`](../../src/model/state_space_sym.cpp) / [`solveLinearSystem`](../../src/model/state_space_sym.cpp)).

| Reference | Contribution | Relation to LGM |
|-----------|--------------|-----------------|
| Geddes, Czapor & Labahn, *Algorithms for Computer Algebra* (1992), **Ch. 9 §9.2** *Linear Equations and Gaussian Elimination* | Symbolic linear equation solving, Gaussian elimination with back substitution on rational coefficients | **Primary citation for elimination passes** (Phase H, output reduction, §7 in [heuristics_formalization.md](heuristics_formalization.md); [Springer](https://link.springer.com/book/10.1007/b102438)) |
| Davenport, Siret & Tournier, *Computer Algebra* (1988) | Systems and algorithms for symbolic computation | Background |
| [SymEngine](https://github.com/symengine/symengine) | Open-source symbolic CAS (C++) | **Implementation** of expand, subs, div — not the graph algorithm |

Altun (2002) specifies **which** constraint equations to build; the elimination step is conventional computer algebra, not a novel graph-theoretic result.

---

## 2. State-space realization methods (general)

| Method | Description | In LGM? |
|--------|-------------|---------|
| Normal tree + cut-set / tie-set | Graph-theoretic selection of states | **Yes** (core) |
| Bond-graph causal assignment | Causality on bonds before equation assembly | No (different formalism) |
| Modelica acausal matching | Compiler assigns causality during index reduction | No (external tools) |
| Lagrangian / Hamiltonian | Variational mechanics | No |
| System identification | Data-driven \(A,B,C,D\) | No |

---

## 3. Software landscape

### 3.1 Comparison table

| Tool | Paradigm | Symbolic state space | Multi-domain | Open source | LGM overlap |
|------|----------|----------------------|--------------|-------------|-------------|
| **LGM** | MIT linear graph | **Yes** (SymEngine) | Yes (TF, gyrator) | **GPL-3.0** | — |
| [20-sim](https://www.20sim.com/) | Bond graph, block diagram | Yes (linearization at operating point) | Yes | No | Bond graph vs. linear graph |
| [OpenModelica](https://openmodelica.org/) | Modelica DAE | Via state selection / linearization | Yes | Yes | Acausal; no normal-tree UI |
| Simulink/Simscape | Block / physical network | `linmod` on Simulink model | Yes | No | Numerical linearization |
| BondLib / BondGraph (Modelica) | Bond graph in Modelica | Compiler-dependent | Yes | Partial | Bond graph notation |
| MATLAB Symbolic Math Toolbox | Manual | User-driven | N/A | No | No graph theory layer |
| Hand derivation (coursework) | MIT 2.151 | Yes (paper) | Yes | N/A | LGM automates this |

### 3.2 20-sim

- First commercial bond-graph environment (since 1995).
- **From Bond Graph to Equations:** causal assignment → junction equations → dynamic equations.
- **State space:** automatic linear state-space model; nonlinear models linearized at a working point; MATLAB export.
- **Gap vs. LGM:** bond-graph semantics, proprietary, no MIT normal-tree pedagogy.

### 3.3 OpenModelica / Modelica

- Acausal modeling; causality from matching (see Alkov, Diekmann & Weidemann, Modelica 2014 — BondGraph library).
- State-space extraction requires compilation (`-d=stateSelect=...`) and often **numerical** linearization for nonlinear models.
- **Gap vs. LGM:** no dedicated linear-graph editor; steeper learning curve for 2.151-style exercises.

### 3.4 Educational tools

Most system-dynamics courses still use **manual** normal-tree derivation or bond-graph homework without a dedicated symbolic linear-graph IDE. LGM fills that niche as **GPL software** with LaTeX and MATLAB export.

---

## 4. LGM positioning

**Original aspects:** MIT linear graph → symbolic state-space pipeline; multi-domain two-ports; normal-tree enumeration; GPL desktop tool.

**Standard aspects:** Normal-tree rules, elemental/continuity/compatibility equations — Altun (2002) and MIT 2.151; symbolic elimination of assembled linear relations — Geddes et al. (1992).

**Related tools:** 20-sim (bond graph), OpenModelica (Modelica DAE) — different formalisms; see comparison table §3.1 and per-system study in [cross_tool_comparison.md](cross_tool_comparison.md).

---

## 5. Bibliography (BibTeX-ready)

```bibtex
@inproceedings{altun2002extraction,
  author    = {Altun, Kerem and Balkan, Raif Tuna and Platin, B{\"u}lent E.},
  title     = {Extraction of State Variable Representations of Dynamic Systems Employing Linear Graph Theory},
  year      = {2002},
  doi       = {10.13140/RG.2.2.29940.96647}
}

@book{paynter1961analysis,
  author    = {Paynter, Henry M.},
  title     = {Analysis and Design of Engineering Systems},
  publisher = {MIT Press},
  year      = {1961}
}

@book{koenig1967analysis,
  author    = {Koenig, H. E. and Tokad, Y. and Kesavan, H. K. and Hedges, H. G.},
  title     = {Analysis of Discrete Physical Systems},
  publisher = {McGraw-Hill},
  year      = {1967}
}

@book{borutzky2010bond,
  author    = {Borutzky, Wolfgang},
  title     = {Bond Graph Methodology},
  publisher = {Springer},
  year      = {2010}
}

@incollection{alkov2014bondgraph,
  author    = {Alkov, Vladimir and Diekmann, Jan and Weidemann, Ralf},
  title     = {A Generalized Power-Based {Modelica} Library with Application to an Industrial Hydraulic Plant},
  booktitle = {Proc. 10th International Modelica Conference},
  year      = {2014}
}

@book{geddes1992algorithms,
  author    = {Geddes, K. O. and Czapor, S. R. and Labahn, G.},
  title     = {Algorithms for Computer Algebra},
  publisher = {Kluwer Academic Publishers},
  address   = {Boston},
  year      = {1992},
  isbn      = {978-0792392594}
}
```

---

## 6. Gaps in the literature

1. Few **open-source** implementations of Altun (2002) with a modern symbolic CAS.
2. Cascaded-transformer continuity at a shared reference node is brief in course notes; LGM heuristics are formalized in [heuristics_formalization.md](heuristics_formalization.md) §3.
3. Cross-formalism studies (linear graph vs. bond graph vs. Modelica) are limited — LGM provides a manual comparison workflow in [cross_tool_comparison.md](cross_tool_comparison.md) with companion models in [comparison_models/](comparison_models/) and CLI goldens in [benchmarks/golden/](benchmarks/golden/).

See [originality_assessment.md](originality_assessment.md) for a detailed originality summary.
