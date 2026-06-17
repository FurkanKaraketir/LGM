# Linear Graph Modeling

Qt6 desktop canvas for drawing linear graphs: draggable nodes on a snapping grid, directed branches as sweeping cubic Bézier curves, and parallel fan-out when multiple branches connect the same node pair.

## Requirements

- CMake 3.16+
- Qt 6 (Widgets)
- C++17 compiler (MinGW or MSVC)

## Build (Windows, Qt MinGW)

Adjust paths if your Qt install differs.

```powershell
$env:PATH = "C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.0\mingw_64\bin;$env:PATH"

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64" `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"

cmake --build build
.\build\LinearGraphModeling.exe
```

## Controls

| Action | Input |
|--------|--------|
| Move node | Drag with left mouse |
| Add node | Double-click empty canvas |
| Connect nodes | Shift+click node A, then node B |
| Multi-select | Drag rubber band on empty canvas |

Branches bow to the left of the directed edge (`from → to`). Multiple branches between the same pair spread perpendicular to the chord.

## Project layout

```
CMakeLists.txt
src/
  main.cpp    # demo graph + window
  canvas.h    # GraphScene, NodeItem, BranchItem
  canvas.cpp  # grid, Bézier geometry, interaction
```

## Curve tuning

In `src/canvas.cpp`:

- `kBowFactor` — arch height as a fraction of chord length (default `0.45`)
- `kLaneSpread` — gap between parallel branches (default `36`)

Made with Qt.