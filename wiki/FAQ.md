# FAQ

## Windows: SmartScreen or “unknown publisher” on the installer

Release builds are not code-signed yet. Click **More info** → **Run anyway**, or use the portable zip from [GitHub Releases](https://github.com/FurkanKaraketir/LGM/releases) and run `LGM.exe` directly.

## macOS: “cannot be opened” or quarantine

Right-click the app → **Open**, or remove quarantine:

```bash
xattr -cr /path/to/LGM.app
```

Use the `.dmg` or portable zip from [GitHub Releases](https://github.com/FurkanKaraketir/LGM/releases).

## Build: Qt or vcpkg not found

Paths are machine-specific. Edit `CMakePresets.json` (`windows-mingw`) or copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and override `CMAKE_PREFIX_PATH`, `CMAKE_TOOLCHAIN_FILE`, and compiler paths. See [Building-from-source](Building-from-source).

First configure needs network access to fetch SymEngine and JKQtPlotter via CMake `FetchContent`.

## Analysis failed — what do the status codes mean?

| Code | Meaning |
|------|---------|
| `NeedNormalTree` | Run **Find Normal Tree** or apply a manual tree before **Compute State Space** |
| `NotConnected` | Graph is not one connected component |
| `ForcedCycle` | A cycle prevents a valid spanning tree |
| `Incomplete` | Tree selection could not be completed |
| `TwoPortConstraint` | Transformer/gyrator causality rules violated — see [Graph-model](Graph-model) and [Analysis](Analysis) |
| `Unsupported` / `SymbolicError` / `GraphError` | State-space derivation could not finish — check branch types, constants, and domain |

Full pipeline: [Analysis](Analysis).

## Command-line analysis

```bash
LGM --analyze Examples/Motor.lgm
LGM --analyze mymodel.lgm OmegaJ i_L
```

Optional arguments after the `.lgm` path are output variable names. Exit code is non-zero on failure. Useful for scripting and quick checks without opening the GUI.

## Where is the documentation synced from?

This wiki is generated from the main repo: `docs/`, `src/assets/guides/`, and `wiki/` via GitHub Actions. In-app help (**Help → Guides**) uses the same guide text bundled in the application.

## MATLAB® export

After a successful **Compute State Space**, use **Export MATLAB® Script** in the Analyze dock. MATLAB and Simulink are registered trademarks of The MathWorks, Inc.
