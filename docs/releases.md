# Releasing LGM

Releases are published from [GitHub](https://github.com/FurkanKaraketir/LGM/releases) when you push a version tag.

## Version numbers

Keep these in sync when bumping a release:

| Location | Example |
|----------|---------|
| `CMakeLists.txt` → `project(LGM VERSION …)` | `0.2.0` |
| `vcpkg.json` → `"version"` | `0.2.0` |
| Git tag | `v0.2.0` |

The app reads its version from CMake via the `LGM_VERSION` compile definition (shown in **Help → About**).

## Release artifacts (per platform)

| Platform | Installer (recommended) | Portable |
|----------|-------------------------|----------|
| Windows | `LGM-x.y.z-win64-setup.exe` (Inno Setup) | `LGM-x.y.z-win64-portable.zip` |
| macOS | `LGM-x.y.z-macos.dmg` (drag to Applications) | `LGM-x.y.z-macos-portable.zip` |
| Linux | `LGM-x.y.z-linux-x86_64.AppImage` (chmod +x, run) | — |

## Cut a release

1. Update version in `CMakeLists.txt` and `vcpkg.json`.
2. Commit and push to `main`.
3. Tag and push:

   ```bash
   git tag v0.2.0
   git push origin v0.2.0
   ```

4. The [Release workflow](.github/workflows/release.yml) builds all platforms with Qt **6.11.0** (same as local dev). CI installs Qt via [aqt](https://github.com/miurahr/aqtinstall) from `master` because the released aqt 3.3.0 does not yet support Qt 6.11’s repository layout.

## Local packaging

**Windows** requires [Inno Setup 6](https://jrsoftware.org/isinfo.php) for `setup.exe`. The portable zip is always produced.

```powershell
cmake --preset windows-mingw
cmake --build build
./scripts/package-windows.ps1 -Version 0.2.0
```

```bash
# macOS (.app bundle required)
cmake --preset ci-macos -DLGM_MACOS_BUNDLE=ON
cmake --build build
./scripts/package-macos.sh 0.2.0 build

# Linux (downloads linuxdeploy on first run)
cmake --preset ci-linux
cmake --build build
./scripts/package-linux.sh 0.2.0 build
```

Installer sources:

- Windows: `scripts/installer/LGM.iss`
- macOS: `scripts/package-macos.sh` (`hdiutil` DMG)
- Linux: `scripts/package-linux.sh` (linuxdeploy AppImage)

## Updates in the app

**Help → Check for Updates** queries the GitHub API for the latest release and opens the release page if a newer version exists. Users download the platform installer and install manually.

To upgrade later: [WinSparkle](https://winsparkle.org/) (Windows), [Sparkle](https://sparkle-project.org/) (macOS), or AppImage zsync (Linux).

## Code signing (recommended before wide distribution)

| Platform | Tool |
|----------|------|
| Windows | `signtool` on `setup.exe` and `LGM.exe` |
| macOS | `codesign` + Apple notarization on `.app` / `.dmg` |
| Linux | Optional for AppImage |

Unsigned builds work but trigger SmartScreen / Gatekeeper warnings. Release CI ad-hoc-signs the macOS `.app` after `macdeployqt` (examples live in `Contents/Resources/`; frameworks and binary signed separately). If macOS still blocks a download, remove quarantine: `xattr -cr /Applications/LGM.app`.

## GPL source offer

Each release notes body should link to the matching source tag, e.g. `https://github.com/FurkanKaraketir/LGM/tree/v0.2.0`.
