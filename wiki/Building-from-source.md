# Building from source

Pre-built installers and portable builds are on [GitHub Releases](https://github.com/FurkanKaraketir/LGM/releases). Use this page when you need to compile LGM locally.

## Requirements

- CMake 3.16+
- Qt **6.11** (Widgets, Network) — same version as CI and `CMakePresets.json`
- C++17 compiler (MinGW or MSVC on Windows; Clang on macOS; GCC on Linux)
- [vcpkg](https://vcpkg.io/) (Boost.Multiprecision)
- Network access on first configure (SymEngine and JKQtPlotter are fetched via CMake `FetchContent`)

## Windows (Qt MinGW)

Machine-specific Qt/vcpkg paths are in the `windows-mingw` preset in `CMakePresets.json`. Edit that preset if your install paths differ, or copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and add overrides.

**Configure once:**

```powershell
cmake --preset windows-mingw
```

**Build and run** (day to day):

```powershell
cmake --build build
.\build\LGM.exe
```

`windeployqt` runs automatically after the build when available, copying Qt runtime DLLs next to the executable. Example graphs from `Examples/` are copied beside the executable at build time.

## macOS

CI uses the `ci-macos` preset with `VCPKG_ROOT` set and vcpkg triplet `arm64-osx`. Install Qt 6.11 and vcpkg, then:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset ci-macos -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.0/macos
cmake --build build
./build/LGM
```

Optional: `-DLGM_MACOS_BUNDLE=ON` for a `.app` bundle (see [Releases](Releases) for packaging).

## Linux

CI uses the `ci-linux` preset with triplet `x64-linux`:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset ci-linux -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.0/gcc_64
cmake --build build
./build/LGM
```

AppImage packaging: `scripts/package-linux.sh` (see [Releases](Releases)).

## CLI mode

After building, you can run headless analysis:

```bash
LGM --analyze path/to/model.lgm [output_var ...]
```

Loads the graph, finds a normal tree, computes state space, and prints results to stdout. See [FAQ](FAQ).

## Project layout

See the [source repository](https://github.com/FurkanKaraketir/LGM#project-layout) README for `src/canvas/`, `src/model/`, and `src/ui/` structure.
