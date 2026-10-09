# Cross-formalism comparison (LGM vs bond graph vs Modelica)

Methodology for comparing **identical physical systems** across three modeling paths. This document is **manual** — companion models live in [`comparison_models/`](comparison_models/); LGM reference output in [`benchmarks/golden/`](benchmarks/golden/).

See also [literature_review.md](literature_review.md) §3 and [heuristics_formalization.md](heuristics_formalization.md) §10 (Altun gaps).

---

## Comparison dimensions

| Dimension | LGM (MIT linear graph) | Bond graph (20-sim / Borutzky) | Modelica (OpenModelica) |
|-----------|------------------------|-------------------------------|-------------------------|
| **Topology UI** | Across/through on nodes & branches | Directed bonds, 0/1-junctions | Acausal components |
| **State selection** | Normal tree + co-tree | Causal stroke assignment | Compiler matching / `stateSelect` |
| **Equation assembly** | Cut-set / tie-set from tree | Junction + constitutive | DAE + index reduction |
| **Symbolic output** | SymEngine `ẋ`, `A`,`B`,`E` | Tool-dependent linearization | `linearize()` at operating point |
| **Multi-domain** | Transformers & gyrators as two-ports | TF, GY multi-bonds | Libraries (e.g. BondGraph, Fluid) |

**Expected result:** for linear models with matched parameters, \(\dim x\) and \((A,B)\) agree up to **state reordering** and sign conventions on through variables.

---

## System 1: PM DC motor

| | LGM | Bond graph | Modelica |
|---|-----|------------|----------|
| **File** | [`Examples/Motor.lgm`](../../Examples/Motor.lgm) | [`comparison_models/Motor_bondgraph.md`](comparison_models/Motor_bondgraph.md) | [`comparison_models/Motor.mo`](comparison_models/Motor.mo) |
| **Golden** | [`benchmarks/golden/Motor.txt`](benchmarks/golden/Motor.txt) | § equations in bondgraph doc | linearize `Motor` |
| **States** | `OmegaJ`, `i_L` | \( \omega \), \( i_L \) (integral causality on I-elements) | `rotor.w`, `winding.i` |
| **Inputs** | `Vs1(t)` | Se effort source | `vs.v` |
| **\(\dot{u}\) in \(\dot{x}\)** | No (`E` absent) | No (effort-out Se) | No |
| **Coupling** | Transformer modulus `TF` | TF / GY bond | `RotationalEMF(k=TF)` |

**LGM golden:**

```
OmegaJ_dot = -B*OmegaJ/J + TF*i_L/J
i_L_dot    = Vs1(t)/L - R*i_L/L - TF*OmegaJ/L
```

**Friction points:** bond-graph TF sign vs LGM port bow direction; reorder \((\omega, i)\) vs \((i, \omega)\) when comparing \(A\) matrices.

---

## System 2: Coupled electrical network

| | LGM | Bond graph | Modelica |
|---|-----|------------|----------|
| **File** | [`Examples/Electrical.lgm`](../../Examples/Electrical.lgm) | (not shipped — derive 0/1-junctions from graph) | [`comparison_models/Electrical.mo`](comparison_models/Electrical.mo) |
| **Golden** | [`benchmarks/golden/Electrical.txt`](benchmarks/golden/Electrical.txt) | — | linearize after parameter fix |
| **States** | `C5_across`, `i_L7` | Typically capacitor voltages & inductor currents (2+) | Compiler-chosen |
| **Inputs** | `Vs1`, `Is2` | Se, Sf sources | `vs1.v`, `is2.i` |
| **\(\dot{u}\) in \(\dot{x}\)** | **Yes** — `Vs1_dot`, `Is2_dot` in golden | Depends on causality | Often yes for voltage/current sources |

**LGM golden (excerpt):**

```
C5_across_dot = ... + C4*Vs1_dot(t)/(C5*(1 + C4/C5)) - ...
i_L7_dot      = ... + L3*Is2_dot(t)/(L7*(1 + L3/L7)) + ...
```

**Friction points:**

- **Synthetic across** (`C5_across = V1 - V2`) is an LGM-specific state coordinate ([heuristics_formalization.md](heuristics_formalization.md) §5). Bond graphs and Modelica usually keep node voltages or individual capacitor voltages — **reorder / eliminate** one voltage before comparing \(A\).
- Parallel `C4`/`C5` on the same span is a **node-reduction choice**, not a different physical model.

---

## System 3: Pump–accumulator (multi-domain)

| | LGM | Bond graph | Modelica |
|---|-----|------------|----------|
| **File** | [`Examples/PumpAccumulator.lgm`](../../Examples/PumpAccumulator.lgm) | Textual derivation from LGM graph | [`comparison_models/PumpAccumulator.mo`](comparison_models/PumpAccumulator.mo) (parameters) |
| **Golden** | [`benchmarks/golden/PumpAccumulator.txt`](benchmarks/golden/PumpAccumulator.txt) | — | Full network not shipped |
| **States** | `Q_L_f`, `i_L`, `Vm` | Domain-specific (fluid inertance, \(i_L\), mass \(v\)) | 3+ if fully modeled |
| **Inputs** | `Fs2`, `Vs1` | Sf / Se per domain | Signal sources |
| **\(\dot{u}\)** | Context-dependent | Causality-dependent | DAE structure |

**LGM golden (coupling terms):**

```
Q_L_f_dot = Fs2(t)/(A*L_f) - R_f*Q_L_f/L_f - i_L/(K_a*K_b*L_f)
i_L_dot   = Vs1(t)/L - R*i_L/L + Q_L_f/(L*K_a*K_b)
Vm_dot    = Fs2(t)/M + A*Q_L_f*R_leak/M - A**2*Vm*R_leak/M
```

**Friction points:**

- **Cascaded transformers** at shared refs ([heuristics_formalization.md](heuristics_formalization.md) §3) — bond graphs use separate 0-junctions; LGM applies `signedSpanJunctionFlowSum`.
- **Mass-in-tree compliance reflection** (§4) — no single bond-graph menu item; equivalent after junction elimination.
- Full Modelica hydraulic network is left to the reader; parameter block documents expected LGM structure.

---

## Summary table (per-system state count)

| System | LGM \(\dim x\) | Bond graph (expected) | Modelica (expected) | Notes |
|--------|----------------|----------------------|---------------------|-------|
| Motor | 2 | 2 | 2 | Full companion models |
| Electrical | 2 | 2–3 before reduction | 2–3 | LGM synthetic across reduces to 2 |
| PumpAccumulator | 3 | 3 (if fully drawn) | 3+ | Partial `.mo`; LGM golden is reference |

---

## Verification procedure (reader)

1. **LGM:** `build\LGM.exe --analyze Examples\<System>.lgm` → compare to [`benchmarks/golden/`](benchmarks/golden/).
2. **Bond graph:** reproduce [`Motor_bondgraph.md`](comparison_models/Motor_bondgraph.md) in 20-sim or by hand; linearize.
3. **Modelica:** translate [`Motor.mo`](comparison_models/Motor.mo); `linearize`; map states to LGM symbols.
4. Compare \(A,B\) allowing permutation matrix \(P\) with \(P A P^{-1}\), \(P B\).

---

## Relation to Altun (2002) and Geddes et al. (1992)

Altun covers **System 1**-style graphs (normal tree, grounded sources). Systems 2–3 require LGM extensions documented in [heuristics_formalization.md](heuristics_formalization.md) §10. Symbolic elimination of assembled constraints follows Geddes, Czapor & Labahn (1992), Ch. 9 §9.2. Bond-graph and Modelica tools implement **different state-selection semantics** but should agree on **input–output linearizations** when the physics and parameters match.
