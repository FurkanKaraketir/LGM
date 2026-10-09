# LGM analysis golden benchmarks

CLI snapshots of `LGM --analyze` output for the example corpus. Used as a lightweight regression anchor (ponytail: no test framework).

## Prerequisites

Build LGM once:

```text
cmake --build build
```

The executable is `build/LGM.exe` on Windows or `build/LGM` on Unix.

## Regenerate

From the repository root:

```powershell
.\docs\publication\benchmarks\generate.ps1
```

Or manually:

```text
build\LGM.exe --analyze Examples\Motor.lgm T_J i_L T_B
```

Stdout lines captured per file:

| Line prefix | Meaning |
|-------------|---------|
| `normal_tree_order=` | Number of state variables |
| `state=` | State symbol |
| `status=` | `0` = Ok |
| `message=` | Summary counts |
| `state_eq=` | State derivative equation |
| `output_eq=` | Output equation (when outputs requested) |
| `output_matrix=` | LaTeX C/D matrix form |

Debug log lines on stderr are **not** included in golden files.

## Golden files

| File | Example | Notes |
|------|---------|-------|
| `golden/Motor.txt` | Motor.lgm | Outputs `T_J`, `i_L`, `T_B` |
| `golden/Electrical.txt` | Electrical.lgm | Synthetic across (`C5_across`) |
| `golden/Gyrator.txt` | Gyrator.lgm | Cross-domain gyrator |
| `golden/TransformerCascade.txt` | TransformerCascade.lgm | Cascaded transformers |
| `golden/PumpAccumulator.txt` | PumpAccumulator.lgm | Multi-domain stress test |
| `golden/FloatingSource.txt` | FloatingSource.lgm | Floating voltage source |
| `golden/MultiNormalTree.txt` | MultiNormalTree.lgm | Multi-tree / TreeScore |

Compare after code changes:

```powershell
.\docs\publication\benchmarks\generate.ps1
git diff docs/publication/benchmarks/golden
```
