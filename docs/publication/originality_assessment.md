# Originality assessment

**Date:** July 2026  
**Project:** LGM (Linear Graph Modeling) v0.2.7

---

## Summary

| Question | Answer |
|----------|--------|
| Novel theory or algorithm? | **No** — framework follows MIT 2.151 and Altun et al. (2002) |
| Original implementation? | **Yes** — symbolic derivator, GUI/CLI, edge-case heuristics |
| Primary theoretical reference | Altun, Balkan & Platin (2002) |
| Symbolic elimination reference | Geddes, Czapor & Labahn (1992), Ch. 9 §9.2 |

---

## What LGM contributes

1. **Symbolic pipeline:** `.lgm` → normal tree → `ẋ = Ax + Bu [+ E u̇]`, `y = Cx + Du [+ F u̇]` (SymEngine).
2. **Multi-domain UI:** mechanical, electrical, fluid, thermal, rotational; transformers and gyrators.
3. **Pedagogical alignment:** MIT 2.151 linear graph formulation; in-app guides; LaTeX/MATLAB export.
4. **Implementation engineering:** TreeScore, normal-tree enumeration, cascaded-transformer KCL, mass–compliance reflection, coupling guards, floating sources ([heuristics_formalization.md](heuristics_formalization.md)).
5. **Validation corpus:** seven example graphs, CLI golden benchmarks ([benchmarks/](benchmarks/)), cross-formalism comparison ([cross_tool_comparison.md](cross_tool_comparison.md)).

---

## Not yet a distinct contribution

- New graph-theoretic theorems or state-selection proofs.
- Numerical simulation, estimation (Kalman), or optimization.
- Automated regression against OpenModelica or 20-sim (manual comparison workflow only).

The theoretical backbone is **Altun (2002)** and **MIT 2.151** course material. Reduction of assembled linear constraints to \(\dot{x}\) and \(y\) in \(\{x,u,\dot{u}\}\) uses **standard symbolic elimination** — Geddes, Czapor & Labahn (1992), Ch. 9 §9.2 *Linear Equations and Gaussian Elimination*; LGM-specific **coupling guards** (§6) filter invalid substitutions.

---

## Follow-on work (addressed July 2026)

| Item | Status | Location |
|------|--------|----------|
| Formalize heuristics on a broader example set | **Done** | [heuristics_formalization.md](heuristics_formalization.md) §§1–10, [`Examples/`](../../Examples/) |
| Document gaps vs Altun (2002) | **Done** | [heuristics_formalization.md](heuristics_formalization.md) §10 |
| Compare with bond-graph / Modelica on identical systems | **Done** (methodology) | [cross_tool_comparison.md](cross_tool_comparison.md), [comparison_models/](comparison_models/) |

See also [literature_review.md](literature_review.md).
