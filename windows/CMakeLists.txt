cmake_minimum_required(VERSION 3.25)

# Static vcruntime, set before project(). See the link options below for the UCRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Noitch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(island_tests tests/island_tests.cpp)
target_include_directories(island_tests PRIVATE src)
add_test(NAME island_tests COMMAND island_tests)

add_executable(Noitch WIN32
    src/main.cpp
    src/noitch.manifest
    src/island/compositor.cpp
    src/island/island_window.cpp
    src/island/header_scene.cpp
    src/island/page_controller.cpp
    src/island/painter.cpp
    src/features/home/home_controller.cpp
    src/features/home/home_scene.cpp
    src/features/usage/claude_limit_source.cpp
    src/features/usage/usage_controller.cpp
    src/features/usage/usage_scene.cpp
    src/features/usage/usage_source.cpp
    src/features/lyrics/lyrics_source.cpp
    src/features/now_playing/media_session_source.cpp
    src/features/settings/settings_store.cpp
    src/features/settings/settings_window.cpp
)
target_include_directories(Noitch PRIVATE src)
target_compile_definitions(Noitch PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 _HAS_EXCEPTIONS=0)
target_link_libraries(Noitch PRIVATE
    d3d11 dcomp shcore user32 gdi32 uuid advapi32 ole32 shell32
    d2d1 dwrite windowscodecs winhttp runtimeobject delayimp
)

if(MSVC)
    target_compile_options(Noitch PRIVATE
        /W4 /permissive- /utf-8
        /GR-        # No RTTI
        /EHs-c-     # No exceptions
        /Gw /Gy
        "$<$<CONFIG:Release>:/O2;/GL>"
    )
    target_link_options(Noitch PRIVATE
        # Static vcruntime with the UCRT that ships in Windows 10+: no redistributable to install,
        # and no copy of the C runtime in the binary.
        "$<$<CONFIG:Release>:/NODEFAULTLIB:libucrt.lib;ucrt.lib>"
        # Everything Home needs costs nothing until the island first opens.
        /DELAYLOAD:d2d1.dll
        /DELAYLOAD:dwrite.dll
        /DELAYLOAD:windowscodecs.dll
        /DELAYLOAD:winhttp.dll
        /DELAYLOAD:api-ms-win-core-winrt-l1-1-0.dll
        /DELAYLOAD:api-ms-win-core-winrt-string-l1-1-0.dll
        "$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>"
    )
endif()
