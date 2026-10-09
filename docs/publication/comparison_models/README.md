# Companion models for cross-formalism comparison

Reference models aligned with LGM examples. Used for **manual** verification against [`benchmarks/golden/`](../benchmarks/golden/) — see [cross_tool_comparison.md](../cross_tool_comparison.md).

| File | LGM example | Formalism |
|------|-------------|-----------|
| [`Motor.mo`](Motor.mo) | Motor.lgm | OpenModelica / Modelica |
| [`Electrical.mo`](Electrical.mo) | Electrical.lgm | OpenModelica (topology sketch) |
| [`PumpAccumulator.mo`](PumpAccumulator.mo) | PumpAccumulator.lgm | OpenModelica (parameter sketch) |
| [`Motor_bondgraph.md`](Motor_bondgraph.md) | Motor.lgm | Bond graph (textual) |

## OpenModelica workflow (reader)

1. Load model, e.g. `Motor.mo`.
2. Set `vs.v` as input (`Vs1(t)`).
3. Simulate or linearize at equilibrium:
   ```modelica
   // After translation:
   // linearize the model or use Modelica_Linearize2
   ```
4. Compare state vector dimension and \(A,B\) matrices with LGM golden `state_eq=` lines (allow state reordering).

Useful flags: `-d=stateSelect=always` (version-dependent) to inspect compiler state choice.

## 20-sim workflow (reader)

1. Enter bond graph from [`Motor_bondgraph.md`](Motor_bondgraph.md).
2. Assign causality (integral on \(L\) and \(J\)).
3. **Run → Linearization** → export state space.
4. Match signs and state order to LGM output.

## Parameter alignment

Use the same symbolic names as in the `.lgm` file (`R`, `L`, `J`, `B`, `TF`, …). LGM exports numeric values only when constants are numeric in the graph.
