function(ckre_configure_test_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CKRE_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKFFPLib/FixedFunction
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKFFPLib/Context
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CKRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
        CKRE_ENABLE_TEST_ACCESS=1
    )

    set(_ckre_test_ck2_dep CK2)
    if (TARGET CK2Static)
        set(_ckre_test_ck2_dep CK2Static)
    endif ()

    set(_ckre_test_vxmath_dep VxMath)
    if (TARGET VxMathStatic)
        set(_ckre_test_vxmath_dep VxMathStatic)
    endif ()

    set(_ckre_test_rasterizer_deps)
    if (TARGET CKBgfxRasterizerStatic)
        list(APPEND _ckre_test_rasterizer_deps CKBgfxRasterizerStatic)
    endif ()

    target_link_libraries(${TARGET_NAME} PRIVATE
        CK2_3DStatic
        CKRasterizerLib
        CKFFPLib
        ckre_recording_support
        ${_ckre_test_rasterizer_deps}
        ${_ckre_test_ck2_dep}
        ${_ckre_test_vxmath_dep}
    )
    ckre_apply_diagnostic_definitions(${TARGET_NAME} PRIVATE)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/CK2_3D.ini"
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/CK2_3D.ini"
        VERBATIM
    )
    ckre_copy_runtime_dependencies(${TARGET_NAME})
endfunction()

# The recording backend is test infrastructure, not a production rasterizer.
# Keeping it in a private test library prevents it from becoming an installed
# backend or an accidental fallback implementation.
add_library(ckre_recording_support STATIC
    support/CKRecordingBackend.cpp
    support/CKRecordingProvider.cpp
)
target_include_directories(ckre_recording_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
)
target_link_libraries(ckre_recording_support PUBLIC CKFFPLib)
target_compile_features(ckre_recording_support PRIVATE cxx_std_20)
set_target_properties(ckre_recording_support PROPERTIES FOLDER "Tests/Support")

function(ckre_add_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    add_test(NAME ${TARGET_NAME} COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT 60)
endfunction()

function(ckre_add_benchmark TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
endfunction()

# The provider-neutral program interface can be tested without a GPU device.
add_executable(test_backend_program test_backend_program.cpp)
target_link_libraries(test_backend_program PRIVATE CKFFPLib)
set_target_properties(test_backend_program PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
ckre_copy_runtime_dependencies(test_backend_program)
add_test(NAME BackendProgramTests COMMAND $<TARGET_FILE:test_backend_program>)
set_tests_properties(BackendProgramTests PROPERTIES TIMEOUT 60)

find_package(Python3 COMPONENTS Interpreter QUIET)
add_test(NAME BgfxDependencyDisabled
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_bgfx_disabled.cmake)
if(TARGET ckre_scene_capture AND TARGET VxMath::stb)
    add_executable(scene_capture_image_tests test_scene_capture_image.cpp ../tools/scene_capture/ImageIO.cpp)
    target_include_directories(scene_capture_image_tests PRIVATE ../tools/scene_capture)
    target_link_libraries(scene_capture_image_tests PRIVATE VxMath VxMath::stb)
    target_compile_features(scene_capture_image_tests PRIVATE cxx_std_17)
    ckre_copy_runtime_dependencies(scene_capture_image_tests)
    add_test(NAME SceneCaptureImageIO COMMAND scene_capture_image_tests)
endif()
if (Python3_Interpreter_FOUND)
    add_test(NAME RasterizerTraceAccounting
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_rasterizer_trace.py)
    add_test(NAME RasterizerModuleStructure
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_rasterizer_boundaries.py
                --source-root ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_tests_properties(RasterizerModuleStructure PROPERTIES TIMEOUT 60)
endif ()

# Tests of the CKRasterizer v3 interface: the translation core (CKFFPLib)
# driven through the public interface only, over the recording device of
# FFPRecordingHarness.h. They do not link the engine.
function(ckre_add_rasterizer3_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKFFPLib/FixedFunction
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKFFPLib/Context
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CKRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    target_link_libraries(${TARGET_NAME} PRIVATE CKFFPLib ckre_recording_support)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "Tests"
    )
    ckre_copy_runtime_dependencies(${TARGET_NAME})
    add_test(NAME ${TARGET_NAME} COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT 60)
endfunction()

add_executable(test_recording_backend test_recording_backend.cpp)
target_link_libraries(test_recording_backend PRIVATE ckre_recording_support)
set_target_properties(test_recording_backend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "Tests"
)
ckre_copy_runtime_dependencies(test_recording_backend)
add_test(NAME RecordingBackendTests COMMAND $<TARGET_FILE:test_recording_backend>)
set_tests_properties(RecordingBackendTests PROPERTIES TIMEOUT 60)

ckre_add_rasterizer3_test(rasterizer3_interface_tests
    test_rasterizer3_interface.cpp
)

# Translation core internals (state forwarding into the fixed-function
# pipeline, device calls) on the same recording device.
ckre_add_rasterizer3_test(rasterizer3_translated_tests
    test_rasterizer3_translated.cpp
)

# Deterministic CKFFPLib workload coverage.  The benchmark target is
# intentionally not registered with CTest and is never installed or staged.
ckre_add_rasterizer3_test(ffp_benchmark_workload_tests
    test_ffp_benchmark_workload.cpp
)

ckre_add_benchmark(ffp_pipeline_benchmark
    benchmark_ffp_pipeline.cpp
    support/CKBenchmarkBackend.cpp
)
target_include_directories(ffp_pipeline_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(ffp_pipeline_benchmark PRIVATE
    CKRE_BENCHMARK_RELEASE=$<IF:$<CONFIG:Release>,1,0>
)
set_target_properties(ffp_pipeline_benchmark PROPERTIES
    FOLDER "Benchmarks"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
# Same-backend golden frame regression (spec 7.3 / plan task 0.7). Needs the
# shared build (ckre_scene_capture loads CK2_3D.dll dynamically) and the same
# environment gates as rasterizer3_pixel_tests. Skips when no golden frames
# exist for the current runner / backend.
if (TARGET ckre_scene_capture AND TARGET CK2_3D AND CKRE_BUILD_SHARED)
    add_test(NAME scene_capture_golden
        COMMAND "${CMAKE_COMMAND}"
            -DTOOL=$<TARGET_FILE:ckre_scene_capture>
            -DENGINE_DIR=$<TARGET_FILE_DIR:CK2_3D>
            -DGOLDEN_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/reference/frames/golden
            -DOUT_DIR=${CMAKE_BINARY_DIR}/scene_capture
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scene_capture_golden.cmake
    )
    set_tests_properties(scene_capture_golden PROPERTIES
        TIMEOUT 900
        SKIP_REGULAR_EXPRESSION "SKIPPED"
    )

    add_test(NAME scene_capture_multiframe
        COMMAND "${CMAKE_COMMAND}"
            -DTOOL=$<TARGET_FILE:ckre_scene_capture>
            -DENGINE_DIR=$<TARGET_FILE_DIR:CK2_3D>
            -DOUT_DIR=${CMAKE_BINARY_DIR}/scene_capture_multiframe
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scene_capture_multiframe.cmake
    )
    set_tests_properties(scene_capture_multiframe PROPERTIES
        TIMEOUT 900
        SKIP_REGULAR_EXPRESSION "SKIPPED"
    )
endif ()

ckre_add_test(nvstripifier_tests
    test_nvstripifier.cpp
)

ckre_add_test(meshstriper_tests
    test_meshstriper.cpp
)

ckre_add_test(geometry_regression_tests
    test_geometry_regressions.cpp
)

ckre_add_test(scene_graph_tests
    test_scene_graph.cpp
)

ckre_add_test(ck2d_entity_tests
    test_ck2d_entity.cpp
)

ckre_add_test(ckmesh_tests
    test_ckmesh.cpp
)

ckre_add_test(cktexture_tests
    test_cktexture.cpp
)

ckre_add_test(simple_mesh_test
    simple_mesh_test.cpp
)

ckre_add_test(material_tests
    test_material.cpp
)

ckre_add_test(draw_state_cache_tests
    test_draw_state_cache.cpp
)

ckre_add_test(ff_uniform_state_tests
    test_ff_uniform_state.cpp
)
if (TARGET CKBgfxRasterizerStatic)
    target_compile_definitions(ff_uniform_state_tests PRIVATE CKRE_TEST_BGFX_ARTIFACTS=1)
endif ()

ckre_add_test(ffp_state_matrix_tests
    test_ffp_state_matrix.cpp
)

# Real-backend pixel gate: fixed-function semantics driven through the public
# rasterizer interface on the bgfx plugin (env-gated, see test_rasterizer3_pixels.cpp).
if (TARGET CKBgfxRasterizerStatic AND TARGET SDL3::SDL3)
    ckre_add_rasterizer3_test(rasterizer3_pixel_tests
        test_rasterizer3_pixels.cpp
    )
    target_include_directories(rasterizer3_pixel_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKBgfxRasterizer
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKBgfxRasterizer/Backend
    )
    target_link_libraries(rasterizer3_pixel_tests PRIVATE
        CKBgfxRasterizerStatic
        SDL3::SDL3
        bgfx
        bimg
        bx
    )
endif ()

if (TARGET CKSdlGpuRasterizerStatic AND TARGET SDL3::SDL3)
    ckre_add_rasterizer3_test(sdl_gpu_ffp_pixel_tests test_rasterizer3_pixels.cpp)
    target_compile_definitions(sdl_gpu_ffp_pixel_tests PRIVATE CKRE_PIXEL_SDL_GPU=1)
    target_link_libraries(sdl_gpu_ffp_pixel_tests PRIVATE CKSdlGpuRasterizerStatic SDL3::SDL3)
    set_tests_properties(sdl_gpu_ffp_pixel_tests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif ()
if (TARGET rasterizer3_pixel_tests)
    set_tests_properties(rasterizer3_pixel_tests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif ()

ckre_add_test(rasterizer_backend_tests
    test_rasterizer_backend.cpp
)

ckre_add_test(ff_state_store_tests
    test_ff_state_store.cpp
)

ckre_add_test(ff_state_resolver_tests
    test_ff_state_resolver.cpp
)

ckre_add_test(ffp_pipeline_tests
    test_ffp_pipeline.cpp
)

ckre_add_test(draw_annotation_tests
    test_draw_annotation.cpp
)

ckre_add_test(render_settings_tests
    test_render_settings.cpp
)

if (TARGET CK2_3DStatic)
    ckre_add_test(static_rasterizer_registration_tests
        test_static_rasterizer_registration.cpp
    )
    target_compile_definitions(static_rasterizer_registration_tests PRIVATE
        CKRE_EXPECT_BGFX=$<BOOL:$<TARGET_EXISTS:CKBgfxRasterizerStatic>>
        CKRE_EXPECT_SDL_GPU=$<BOOL:$<TARGET_EXISTS:CKSdlGpuRasterizerStatic>>)

endif ()

if (CKRE_ENABLE_FRAME_COST_STATS)
    ckre_add_test(render_frame_cost_stats_tests
        test_render_frame_cost_stats.cpp
    )
endif ()

ckre_add_test(render_transient_geometry_tests
    test_transient_geometry.cpp
)

add_executable(test_render_profile_disabled test_render_profile_disabled.cpp)
target_include_directories(test_render_profile_disabled PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib)
target_compile_definitions(test_render_profile_disabled PRIVATE CKRE_ENABLE_PROFILING=0)
set_target_properties(test_render_profile_disabled PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
add_test(NAME RenderProfileDisabled COMMAND $<TARGET_FILE:test_render_profile_disabled>)
