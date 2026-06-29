# Example graphs

Sample `.lgm` files for Linear Graph Modeling. Open with **File → Open** or drag into the app.

| File | Description |
|------|-------------|
| [Motor.lgm](Motor.lgm) | Permanent-magnet DC motor: `R`, `L`, voltage source, transformer `1/Ka`, inertia `J`, damper `B`. Good first analysis example — see [docs/analysis.md](../docs/analysis.md). |
| [Electrical.lgm](Electrical.lgm) | Electrical network with sources and passive branches (RLC-style topology). |
| [Gyrator.lgm](Gyrator.lgm) | Hydraulic actuator: fluid pressure source, `R`, compliance, gyrator modulus `1/A`, and parallel mechanical `m`–`k`–`b`. Cross-domain coupling — both gyrator port branches must be in the normal tree or both in the co-tree. |
| [TransformerCascade.lgm](TransformerCascade.lgm) | Multi-section graph with cascaded transformers and mixed mechanical/electrical elements (higher complexity). |

## Tips

- After opening, run **Find Normal Tree**, optionally select **output variables** in Analyze, then **Compute State Space**.
- Rename branches and edit constants in **Properties** before analysis.
- Use **Domain** on the toolbar if variable labels do not match your intended energy domain.

## Creating your own

1. **File → New**, pick a domain, draw the graph.
2. **File → Save As** → `MyModel.lgm`.
3. Keep files in this folder or anywhere on disk — the format is portable JSON.
