cmake_minimum_required(VERSION 3.16)

set(VCPKG_TARGET_TRIPLET  "x64-mingw-dynamic"                        CACHE STRING "")
set(VCPKG_HOST_TRIPLET    "x64-mingw-dynamic"                        CACHE STRING "")
set(CMAKE_C_COMPILER      "C:/Qt/Tools/mingw1310_64/bin/gcc.exe"     CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER    "C:/Qt/Tools/mingw1310_64/bin/g++.exe"     CACHE FILEPATH "")
set(CMAKE_MAKE_PROGRAM    "C:/Qt/Tools/Ninja/ninja.exe"              CACHE FILEPATH "")
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/mingw_64")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

project(LinearGraphModeling VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Boost.Multiprecision via vcpkg
find_package(Boost REQUIRED COMPONENTS multiprecision)

# SymEngine via FetchContent (not in vcpkg)
include(FetchContent)

set(BUILD_TESTS      OFF CACHE BOOL "")
set(BUILD_BENCHMARKS OFF CACHE BOOL "")
set(WITH_GMP         OFF CACHE BOOL "")
set(INTEGER_CLASS    boostmp CACHE STRING "")

FetchContent_Declare(
    symengine
    GIT_REPOSITORY https://github.com/symengine/symengine.git
    GIT_TAG        v0.14.0
)
FetchContent_MakeAvailable(symengine)

set(JKQtPlotter_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_LIB_JKQTPLOTTER OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    jkqtplotter
    GIT_REPOSITORY https://github.com/jkriege2/JKQtPlotter.git
    GIT_TAG        v5.0.0-beta1
)
FetchContent_MakeAvailable(jkqtplotter)

add_executable(LinearGraphModeling
    src/ui/main.cpp
    src/canvas/canvas.h
    src/canvas/canvas_items.cpp
    src/canvas/canvas_undo.cpp
    src/canvas/canvas_scene_graph.cpp
    src/canvas/canvas_scene_delete.cpp
    src/canvas/canvas_document.cpp
    src/canvas/canvas_scene_input.cpp
    src/canvas/canvas_view.cpp
    src/model/elemental_equation.cpp
    src/model/elemental_equation.h
    src/model/normal_tree.cpp
    src/model/normal_tree.h
    src/model/state_space.cpp
    src/model/state_space_eliminate.cpp
    src/model/state_space_graph.cpp
    src/model/state_space_latex.cpp
    src/model/state_space_sym.cpp
    src/model/state_space.h
    src/ui/latex_widget.cpp
    src/ui/latex_widget.h
    src/ui/app_settings.cpp
    src/ui/app_settings.h
    src/ui/settings_window.cpp
    src/ui/settings_window.h
    src/ui/mainwindow.cpp
)

target_link_libraries(LinearGraphModeling PRIVATE
    Qt6::Widgets
    symengine
    Boost::multiprecision
    JKQTPlotter6::JKQTMathText6
)
target_include_directories(LinearGraphModeling PRIVATE
    src/canvas
    src/model
    src/ui
    ${symengine_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/symengine-build
)

if(WIN32)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET LinearGraphModeling POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations "$<TARGET_FILE:LinearGraphModeling>"
            COMMENT "Deploying Qt runtime DLLs")
    endif()
endif()
