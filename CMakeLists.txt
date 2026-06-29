cmake_minimum_required(VERSION 3.16)

project(LGM VERSION 0.2.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

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

option(LGM_MACOS_BUNDLE "Build LGM as a macOS .app bundle (for release packaging)" OFF)

add_executable(LGM WIN32
    src/ui/main.cpp
    src/canvas/canvas.h
    src/canvas/items/geometry.cpp
    src/canvas/items/branch_item.cpp
    src/canvas/items/node_item.cpp
    src/canvas/items/two_port_item.cpp
    src/canvas/undo/commands.cpp
    src/canvas/scene/scene_detail.cpp
    src/canvas/scene/scene_lifecycle.cpp
    src/canvas/scene/scene_normal_tree.cpp
    src/canvas/scene/scene_merge.cpp
    src/canvas/canvas_scene_delete.cpp
    src/canvas/document/load_helpers.cpp
    src/canvas/document/document_io.cpp
    src/canvas/canvas_scene_input.cpp
    src/canvas/canvas_view.cpp
    src/model/elemental_equation.h
    src/model/elemental_equation/constants.cpp
    src/model/elemental_equation/naming.cpp
    src/model/elemental_equation/symbols.cpp
    src/model/elemental_equation/topology.cpp
    src/model/normal_tree.h
    src/model/normal_tree/normal_tree_detail.h
    src/model/normal_tree/build.cpp
    src/model/normal_tree/enumerate.cpp
    src/model/normal_tree/normal_tree.cpp
    src/model/state_space.h
    src/model/state_space/state_space_context.h
    src/model/state_space/state_space.cpp
    src/model/state_space/state_space_context.cpp
    src/model/state_space/state_space_constraints.cpp
    src/model/state_space/state_space_reflect.cpp
    src/model/state_space/state_space_states.cpp
    src/model/state_space/state_space_reduce.cpp
    src/model/state_space/state_space_matrix.cpp
    src/model/state_space_eliminate.cpp
    src/model/state_space_graph.cpp
    src/model/state_space_latex.cpp
    src/model/state_space_matlab.cpp
    src/model/state_space_sym.cpp
    src/ui/latex_widget.cpp
    src/ui/latex_widget.h
    src/ui/app_log.cpp
    src/ui/app_log.h
    src/ui/app_settings.cpp
    src/ui/app_settings.h
    src/ui/app_shortcuts.cpp
    src/ui/app_shortcuts.h
    src/ui/app_update.cpp
    src/ui/app_update.h
    src/ui/settings_window.cpp
    src/ui/settings_window.h
    src/ui/analyze_window.cpp
    src/ui/analyze_window.h
    src/ui/guide_window.cpp
    src/ui/guide_window.h
    src/ui/mainwindow.h
    src/ui/mainwindow/mainwindow.cpp
    src/ui/mainwindow/detail.cpp
    src/ui/mainwindow/file.cpp
    src/ui/mainwindow/chrome/menu.cpp
    src/ui/mainwindow/chrome/toolbar.cpp
    src/ui/mainwindow/chrome/docks.cpp
    src/ui/mainwindow/chrome/theme.cpp
    src/ui/mainwindow/panels/object_tree.cpp
    src/ui/mainwindow/panels/property_panel.cpp
    src/ui/mainwindow/panels/panel_tabs.cpp
    src/ui/mainwindow/panels/state_space_panel.cpp
    src/ui/tool_icons.cpp
    src/ui/tool_icons.h
    src/assets/assets.qrc
)

target_link_libraries(LGM PRIVATE
    Qt6::Widgets
    Qt6::Network
    symengine
    Boost::multiprecision
    JKQTPlotter6::JKQTMathText6
)
target_include_directories(LGM PRIVATE
    src
    src/canvas
    src/canvas/items
    src/model
    src/ui
    src/ui/mainwindow
    ${symengine_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/symengine-build
)

target_compile_definitions(LGM PRIVATE
    "EXAMPLES_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/Examples\""
    "LGM_VERSION=\"${PROJECT_VERSION}\""
    "LGM_GITHUB_REPO=\"FurkanKaraketir/LGM\"")

if(WIN32)
    target_sources(LGM PRIVATE src/assets/app.rc)
endif()

if(APPLE AND LGM_MACOS_BUNDLE)
    set_target_properties(LGM PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "LGM"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.furkankaraketir.lgm"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

function(lgm_deploy_qt target)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qmake)
        return()
    endif()
    get_filename_component(_qt_bin_dir "${_qmake}" DIRECTORY)
    if(APPLE AND LGM_MACOS_BUNDLE)
        find_program(MACDEPLOYQT macdeployqt HINTS "${_qt_bin_dir}")
        if(MACDEPLOYQT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:${target}>" -always-overwrite
                COMMENT "Deploying Qt frameworks into .app bundle")
        endif()
    elseif(WIN32)
        find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
        if(WINDEPLOYQT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${WINDEPLOYQT}" --no-translations "$<TARGET_FILE:${target}>"
                COMMENT "Deploying Qt runtime DLLs")
        endif()
    endif()
endfunction()
lgm_deploy_qt(LGM)

if(APPLE AND LGM_MACOS_BUNDLE)
    set(_lgm_examples_dest "$<TARGET_BUNDLE_DIR:LGM>/Contents/Resources/Examples")
else()
    set(_lgm_examples_dest "$<TARGET_FILE_DIR:LGM>/Examples")
endif()
add_custom_command(TARGET LGM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Examples" "${_lgm_examples_dest}"
    COMMENT "Copying example graphs")
