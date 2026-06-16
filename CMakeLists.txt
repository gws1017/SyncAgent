cmake_minimum_required(VERSION 3.20)
project(syncagent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

add_executable(syncagent WIN32
    src/main.cpp
    src/game.cpp
    src/equipment.cpp
    src/tray.cpp
    src/dashboard.cpp
    src/platform_win.cpp
    src/app.rc
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(syncagent PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(syncagent PRIVATE
    user32
    shell32
    gdi32
    d3d11
    dxgi
    d3dcompiler
)

target_compile_definitions(syncagent PRIVATE UNICODE _UNICODE)

# game.cpp/equipment.cpp는 표준 CRT 함수(fscanf/sscanf/swprintf)를 의도적으로 사용함
# (MSVC 전용 _s 버전은 Android NDK에 없어서 모바일 포팅이 막힘). 디프리케이션 경고 끔.
if (MSVC)
    target_compile_definitions(syncagent PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    target_compile_options(syncagent PRIVATE /W4 /permissive- /utf-8)
endif()
