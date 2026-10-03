# ckre_scene_capture - reference frame / capability capture tool.
#
# Boots CK2 the way the Player does, loads a render engine DLL from a
# directory given on the command line, renders procedural scenes built with
# the public CK2 API, writes PNG frames and a caps JSON, and compares PNG
# sets. It links only CK2, VxMath and SDL3.
#
# Two build modes:
#   * as part of CKRenderEngine (add_subdirectory): links the Ballanced CK2 /
#     VxMath and drives CK2_3D with the selected rasterizer provider;
#   * standalone against the Virtools SDK 2.1 (cmake -S tools/scene_capture
#     -A Win32 -DVIRTOOLS_SDK_PATH=...): links the original CK2.lib /
#     VxMath.lib import libraries so the same binary drives the original
#     CK2.dll + CK2_3D.dll + CKDX8Rasterizer.dll - the visual oracle. The
#     original DLLs cannot bind to the Ballanced CK2.dll (74 + 55 missing
#     exports, see tools/check_dll_imports.py), hence the second mode.

cmake_minimum_required(VERSION 3.16)

set(_ckre_scene_capture_standalone OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_ckre_scene_capture_standalone ON)
    project(ckre_scene_capture LANGUAGES C CXX)

    if (NOT VIRTOOLS_SDK_PATH)
        if (DEFINED ENV{VIRTOOLS_SDK_PATH})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK_PATH}" CACHE PATH "Path to the Virtools SDK 2.1")
        else ()
            set(VIRTOOLS_SDK_PATH "" CACHE PATH "Path to the Virtools SDK 2.1")
        endif ()
    endif ()
    if (NOT VIRTOOLS_SDK_PATH OR NOT EXISTS "${VIRTOOLS_SDK_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "Standalone ckre_scene_capture needs -DVIRTOOLS_SDK_PATH=<Virtools SDK 2.1 checkout> (Include/, Lib/CK2.lib, Lib/VxMath.lib)")
    endif ()
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The original Virtools DLLs are 32-bit: configure with -A Win32")
    endif ()
    # The SDK's own CMakeLists creates directory-scoped imported targets, so
    # link the import libraries directly instead of add_subdirectory().
    set(_ckre_tool_include_dirs "${VIRTOOLS_SDK_PATH}/Include")
    set(_ckre_tool_ck2_dep "${VIRTOOLS_SDK_PATH}/Lib/CK2.lib")
    set(_ckre_tool_vxmath_dep "${VIRTOOLS_SDK_PATH}/Lib/VxMath.lib")
    find_package(SDL3 CONFIG REQUIRED)

    if (MSVC)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    endif ()

    function(ckre_copy_runtime_dependencies TARGET_NAME)
        if (WIN32 AND TARGET SDL3::SDL3)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    $<TARGET_FILE:SDL3::SDL3>
                    $<TARGET_FILE_DIR:${TARGET_NAME}>
                    VERBATIM
            )
        endif ()
    endfunction()
endif ()

# PNG I/O uses VxMath's public header-only stb dependency, never bimg sources.
if(TARGET VxMath::stb)
    set(_ckre_png_dep VxMath::stb)
else()
    # The original SDK-only capture tool does not build VxMath. Accept an
    # independently supplied stb installation in that mode.
    find_path(CKRE_SCENE_CAPTURE_STB_DIR stb_image.h
        HINTS "${STB_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../VxMath/deps/stb"
        PATH_SUFFIXES stb)
    if(NOT CKRE_SCENE_CAPTURE_STB_DIR OR NOT EXISTS "${CKRE_SCENE_CAPTURE_STB_DIR}/stb_image_write.h")
        message(FATAL_ERROR "Scene capture needs stb headers; set CKRE_SCENE_CAPTURE_STB_DIR")
    endif()
    add_library(ckre_scene_capture_stb INTERFACE)
    target_include_directories(ckre_scene_capture_stb INTERFACE "${CKRE_SCENE_CAPTURE_STB_DIR}")
    set(_ckre_png_dep ckre_scene_capture_stb)
endif()

add_executable(ckre_scene_capture
        main.cpp
        CaptureApp.cpp
        CaptureApp.h
        ImageIO.cpp
        ImageIO.h
        SceneRegistry.cpp
        SceneRegistry.h
        SceneUtil.cpp
        SceneUtil.h
        Scenes3D.cpp
        Scenes2D.cpp
        ScenesRtt.cpp
        ScenesStencil.cpp
        ScenesJit.cpp
)
target_compile_features(ckre_scene_capture PRIVATE cxx_std_20)
target_include_directories(ckre_scene_capture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (_ckre_scene_capture_standalone)
    target_compile_definitions(ckre_scene_capture PRIVATE CKRE_SCENE_CAPTURE_VIRTOOLS_SDK=1)
    target_include_directories(ckre_scene_capture PRIVATE ${_ckre_tool_include_dirs})
    if (MSVC)
        # The SDK headers initialize char* from string literals.
        target_compile_options(ckre_scene_capture PRIVATE /Zc:strictStrings-)
    endif ()
else ()
    set(_ckre_tool_ck2_dep CK2)
    set(_ckre_tool_vxmath_dep VxMath)
endif ()

target_link_libraries(ckre_scene_capture PRIVATE
        ${_ckre_png_dep}
        ${_ckre_tool_ck2_dep}
        ${_ckre_tool_vxmath_dep}
        SDL3::SDL3
)
if (CMAKE_DL_LIBS)
    target_link_libraries(ckre_scene_capture PRIVATE ${CMAKE_DL_LIBS})
endif ()

set_target_properties(ckre_scene_capture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Tools"
)
ckre_copy_runtime_dependencies(ckre_scene_capture)

if(CKRE_BUILD_TESTS AND TARGET CKSdlGpuRasterizer)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME scene_benchmark_jit_report
            COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_scene_benchmark_jit.py")
        add_test(NAME scene_capture_jit
            COMMAND "${CMAKE_COMMAND}"
                -DPYTHON=${Python3_EXECUTABLE}
                -DTOOL=$<TARGET_FILE:ckre_scene_capture>
                -DENGINE_DIR=$<TARGET_FILE_DIR:CK2_3D>
                -DOUT_DIR=${CMAKE_BINARY_DIR}/scene_capture_jit
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/scene_capture_jit.cmake)
        set_tests_properties(scene_capture_jit PROPERTIES
            RUN_SERIAL TRUE TIMEOUT 1800 SKIP_REGULAR_EXPRESSION "SKIPPED:")
    endif()
endif()

if (NOT _ckre_scene_capture_standalone AND CKRE_INSTALL)
    install(TARGETS ckre_scene_capture RUNTIME DESTINATION tools COMPONENT Development)
endif ()
