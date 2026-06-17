cmake_minimum_required(VERSION 3.16)
project(LinearGraphModeling VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(LinearGraphModeling
    src/main.cpp
    src/canvas.h
    src/canvas_items.cpp
    src/canvas_undo.cpp
    src/canvas_scene_graph.cpp
    src/canvas_scene_delete.cpp
    src/canvas_scene_input.cpp
    src/canvas_view.cpp
    src/mainwindow.cpp
)

target_link_libraries(LinearGraphModeling PRIVATE Qt6::Widgets)

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
