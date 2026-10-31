# Linear Graph Modeling (LGM)

A Qt6 desktop editor for **MIT-style linear graphs**: draw nodes, directed branches, and two-port elements on a snapping grid, then derive **normal trees** and **state-space equations** from the model. LGM targets coursework-style system dynamics (mechanical, electrical, fluid, thermal, and rotational domains) following the linear graph formulation used in MIT 2.151.

![LGM electrical example](https://raw.githubusercontent.com/FurkanKaraketir/LGM/master/src/assets/screenshot_electrical_example.png)

## Download

**Latest:** [v0.4.2](https://github.com/FurkanKaraketir/LGM/releases/tag/v0.4.2)

| Platform | Installer | Portable |
|----------|-----------|----------|
| Windows | `LGM-x.y.z-win64-setup.exe` | `LGM-x.y.z-win64-portable.zip` |
| macOS 13+ (Apple Silicon) | `LGM-x.y.z-macos-arm64.dmg` | `LGM-x.y.z-macos-arm64-portable.zip` |
| macOS 13+ (Intel) | `LGM-x.y.z-macos-x86_64.dmg` | `LGM-x.y.z-macos-x86_64-portable.zip` |
| Linux (glibc 2.35+: Ubuntu 22.04, Debian 12 or newer) | `LGM-x.y.z-linux-x86_64.AppImage` | — |

All builds are on [GitHub Releases](https://github.com/FurkanKaraketir/LGM/releases). In-app: **Help → Check for Updates**.

## Start here

1. [Quick-start](Quick-start) — canvas controls and typical workflow
2. [Canvas-editor](Canvas-editor) — tools, shortcuts, docks, `.lgm` files
3. [Graph-model](Graph-model) — domains, A/T/D types, two-ports
4. [Analysis](Analysis) — normal trees, state-space derivation, MATLAB® export
5. [Examples](Examples) — sample motor, pump–accumulator, RLC, gyrator, transformer cascade, floating-source, and multiple-normal-tree graphs

## Theory

- [Linear-graph-basics](Linear-graph-basics) — MIT 2.151 handout on state equations from linear graphs
- [Two-ports-basics](Two-ports-basics) — transformers, gyrators, multi-domain graphs
- [State-space-derivation](State-space-derivation) — algorithm walkthrough (in-app guide)
- [Normal-tree-enumeration](Normal-tree-enumeration) — finding multiple normal trees

## Developers

- [Building-from-source](Building-from-source) — CMake, Qt 6.11, vcpkg
- [State-space-algorithm](State-space-algorithm) — implementation reference with source links
- [Releases](Releases) — versioning, CI, packaging

## Project links

- [Source repository](https://github.com/FurkanKaraketir/LGM)
- [Issues](https://github.com/FurkanKaraketir/LGM/issues)
- [Releases](https://github.com/FurkanKaraketir/LGM/releases)
- [License (GPL v3)](https://github.com/FurkanKaraketir/LGM/blob/master/COPYING)
- [FAQ](FAQ)

Background: Altun, Kerem; Balkan, R. Tuna; & Platin, Bülent (2002), *Extraction of state variable representations of dynamic systems employing linear graph theory* ([doi:10.13140/RG.2.2.29940.96647](https://doi.org/10.13140/RG.2.2.29940.96647)). Symbolic elimination: Geddes, Czapor & Labahn (1992), *Algorithms for Computer Algebra*, Ch. 9 §9.2 ([Springer](https://link.springer.com/book/10.1007/b102438)).
