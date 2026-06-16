cmake_minimum_required(VERSION 3.20)
project(idlegame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

add_executable(idlegame WIN32
    src/main.cpp
    src/game.cpp
    src/equipment.cpp
    src/tray.cpp
    src/dashboard.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(idlegame PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(idlegame PRIVATE
    user32
    shell32
    gdi32
    d3d11
    dxgi
    d3dcompiler
)

target_compile_definitions(idlegame PRIVATE UNICODE _UNICODE)

if (MSVC)
    target_compile_options(idlegame PRIVATE /W4 /permissive- /utf-8)
endif()
