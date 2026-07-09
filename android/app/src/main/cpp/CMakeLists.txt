cmake_minimum_required(VERSION 3.22.1)
project(syncagent)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 프로젝트 루트 (android/app/src/main/cpp/ 에서 5단계 위)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)
set(SRC   ${ROOT}src/)
set(IMGUI ${ROOT}vendor/imgui/)

add_library(syncagent SHARED
    main_android.cpp
    ${SRC}game.cpp
    ${SRC}equipment.cpp
    ${SRC}lang.cpp
    ${SRC}dashboard.cpp
    ${SRC}platform_android.cpp
    ${IMGUI}imgui.cpp
    ${IMGUI}imgui_draw.cpp
    ${IMGUI}imgui_tables.cpp
    ${IMGUI}imgui_widgets.cpp
    ${IMGUI}backends/imgui_impl_android.cpp
    ${IMGUI}backends/imgui_impl_opengl3.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

# NativeActivity 진입점 심볼 강제 노출 (링커가 제거하지 않도록)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

target_compile_definitions(syncagent PRIVATE
    IMGUI_IMPL_OPENGL_ES3
)

target_include_directories(syncagent PRIVATE
    ${SRC}
    ${IMGUI}
    ${IMGUI}backends
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(syncagent PRIVATE
    android
    EGL
    GLESv3
    log
)
