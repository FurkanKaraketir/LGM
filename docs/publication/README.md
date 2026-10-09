# Technical analysis

LGM originality analysis notes (July 2026).

| Document | Contents |
|----------|----------|
| [ecc27_paper_draft.md](ecc27_paper_draft.md) | **ECC27 conference paper draft** (abstract + full sections) |
| [originality_assessment.md](originality_assessment.md) | What is standard vs. original |
| [heuristics_formalization.md](heuristics_formalization.md) | LGM-specific heuristics — formal definitions, traces, Altun gap table |
| [literature_review.md](literature_review.md) | Literature and tool comparison |
| [cross_tool_comparison.md](cross_tool_comparison.md) | LGM vs bond graph vs Modelica on identical systems |
| [benchmarks/](benchmarks/) | CLI golden outputs (`generate.ps1`) |
| [comparison_models/](comparison_models/) | Companion `.mo` and bond-graph reference |

## Summary

LGM applies **MIT linear graph theory** (Altun et al., 2002; MIT 2.151) as an open-source symbolic state-space derivation tool. Constraint reduction uses **standard symbolic elimination** (Geddes, Czapor & Labahn, 1992, Ch. 9 §9.2). The core graph theory is established in the literature; the original work here is the software implementation, edge-case heuristics, and the validation / cross-formalism documentation bundle above.
