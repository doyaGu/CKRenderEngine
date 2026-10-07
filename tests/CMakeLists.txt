function(ckre_configure_test_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CKRE_INCLUDE_DIR}
    )
    ckre_apply_diagnostic_definitions(${TARGET_NAME} PRIVATE)
    ckre_apply_platform_definitions(${TARGET_NAME} PRIVATE)
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

function(ckre_register_test TARGET_NAME)
    add_test(NAME ${TARGET_NAME} COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT 60)
endfunction()

# These tests intentionally inspect the test-owned recording Context.
# Keep its private header unavailable to every other test target.
function(ckre_enable_recording_context_white_box TARGET_NAME)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CKRE_RECORDING_CONTEXT_WHITE_BOX=1
    )
endfunction()

# The recording backend is test infrastructure, not a production rasterizer.
# Keeping it in a private test library prevents it from becoming an installed
# backend or an accidental fallback implementation.
add_library(ckre_recording_support STATIC
    support/FFPRecordingContext.cpp
    support/FFPRecordingFrame.cpp
    support/FFPRecordingPresentStage.cpp
    support/FFPRecordingReadback.cpp
    support/FFPRecordingResources.cpp
    support/FFPRecordingTargets.cpp
    support/FFPRecordingTransfer.cpp
    support/CKRecordingBackend.cpp
    support/CKRecordingRasterizer.cpp
)
target_include_directories(ckre_recording_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
)
target_link_libraries(ckre_recording_support PUBLIC CKFFPLib)
target_compile_features(ckre_recording_support PRIVATE cxx_std_20)
set_target_properties(ckre_recording_support PROPERTIES FOLDER "Tests/Support")

function(ckre_add_engine_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE CK2_3DStatic)
    ckre_register_test(${TARGET_NAME})
endfunction()

function(ckre_add_rasterizer_library_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE CKRasterizerLib)
    ckre_register_test(${TARGET_NAME})
endfunction()

function(ckre_add_ckff_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE CKFFPLib)
    ckre_register_test(${TARGET_NAME})
endfunction()

function(ckre_add_recording_white_box_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    ckre_enable_recording_context_white_box(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE ckre_recording_support)
    ckre_register_test(${TARGET_NAME})
endfunction()

function(ckre_add_recording_ckff_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE ckre_recording_support)
    ckre_register_test(${TARGET_NAME})
endfunction()

function(ckre_add_engine_recording_white_box_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    ckre_enable_recording_context_white_box(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE
        CK2_3DStatic
        ckre_recording_support
    )
    ckre_register_test(${TARGET_NAME})
endfunction()

# CPU program validation and constant packing do not require a GPU device.
add_executable(test_ff_program test_ff_program.cpp)
target_link_libraries(test_ff_program PRIVATE CKFFPLib)
set_target_properties(test_ff_program PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
ckre_copy_runtime_dependencies(test_ff_program)
add_test(NAME FFProgramTests COMMAND $<TARGET_FILE:test_ff_program>)
set_tests_properties(FFProgramTests PROPERTIES TIMEOUT 60)

# The shader compiler is CPU-only as well; CK2 headers serve the harness.
function(ckre_add_shader_jit_test TARGET_NAME TEST_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_link_libraries(${TARGET_NAME} PRIVATE CKShaderJit)
    target_include_directories(${TARGET_NAME} PRIVATE $<TARGET_PROPERTY:CK2,INTERFACE_INCLUDE_DIRECTORIES>)
    set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
    ckre_copy_runtime_dependencies(${TARGET_NAME})
    add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 60)
endfunction()

ckre_add_shader_jit_test(test_shader_jit_ir ShaderJitIRTests test_shader_jit_ir.cpp)
ckre_add_shader_jit_test(test_shader_jit_spirv ShaderJitSpirvTests test_shader_jit_spirv.cpp)
if (CKRE_ENABLE_DIRECTX)
    ckre_add_shader_jit_test(test_shader_jit_dxbc ShaderJitDxbcTests test_shader_jit_dxbc.cpp)
endif ()

# Validation tests run a test target with an output directory, then check
# every shader it writes there: the Vulkan SDK validator the SPIR-V modules,
# D3D12 on WARP the DXBC containers.
find_program(CKRE_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if (CKRE_ENABLE_DIRECTX)
    add_executable(shader_jit_dxbc_validator shader_jit_dxbc_validator.cpp)
    target_link_libraries(shader_jit_dxbc_validator PRIVATE CKShaderJit d3d12 dxgi d3dcompiler)
    set_target_properties(shader_jit_dxbc_validator PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
    ckre_copy_runtime_dependencies(shader_jit_dxbc_validator)
endif ()

function(ckre_add_spirv_validation TEST_NAME TARGET_NAME OUT_NAME)
    if (NOT CKRE_SPIRV_VAL)
        return()
    endif ()
    add_test(NAME ${TEST_NAME}
        COMMAND "${CMAKE_COMMAND}"
            -DTEST=$<TARGET_FILE:${TARGET_NAME}>
            -DVALIDATOR=${CKRE_SPIRV_VAL}
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/${OUT_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/shader_jit_spirv_validation.cmake
    )
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 60)
endfunction()

function(ckre_add_dxbc_validation TEST_NAME TARGET_NAME OUT_NAME)
    if (NOT TARGET shader_jit_dxbc_validator)
        return()
    endif ()
    add_test(NAME ${TEST_NAME}
        COMMAND "${CMAKE_COMMAND}"
            -DTEST=$<TARGET_FILE:${TARGET_NAME}>
            -DVALIDATOR=$<TARGET_FILE:shader_jit_dxbc_validator>
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/${OUT_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/shader_jit_dxbc_validation.cmake
    )
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 60)
endfunction()

ckre_add_spirv_validation(ShaderJitSpirvValidation test_shader_jit_spirv shader_jit_spirv)
ckre_add_dxbc_validation(ShaderJitDxbcValidation test_shader_jit_dxbc shader_jit_dxbc)

find_package(Python3 COMPONENTS Interpreter QUIET)
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
endif ()

# White-box conformance tests drive CKRasterizerContext through its public
# methods, then inspect test-owned Context state or recording output.
# They do not link the render engine.
function(ckre_add_recording_context_white_box_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    ckre_configure_test_target(${TARGET_NAME})
    ckre_enable_recording_context_white_box(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE ckre_recording_support)
    ckre_register_test(${TARGET_NAME})
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

ckre_add_recording_context_white_box_test(rasterizer3_interface_tests
    test_rasterizer3_interface.cpp
)

# Recording Context internals (state forwarding into the fixed-function
# pipeline and recording device calls).
ckre_add_recording_context_white_box_test(rasterizer3_translated_tests
    test_rasterizer3_translated.cpp
)

# Deterministic CKFFPLib workload coverage.  The benchmark target is
# intentionally not registered with CTest and is never installed or staged.
ckre_add_recording_context_white_box_test(ffp_benchmark_workload_tests
    test_ffp_benchmark_workload.cpp
)

add_executable(ffp_pipeline_benchmark
    benchmark_ffp_pipeline.cpp
    support/CKBenchmarkBackend.cpp
)
ckre_configure_test_target(ffp_pipeline_benchmark)
ckre_enable_recording_context_white_box(ffp_pipeline_benchmark)
target_link_libraries(ffp_pipeline_benchmark PRIVATE ckre_recording_support)
target_compile_definitions(ffp_pipeline_benchmark PRIVATE
    CKRE_BENCHMARK_RELEASE=$<IF:$<CONFIG:Release>,1,0>
)
set_target_properties(ffp_pipeline_benchmark PROPERTIES
    FOLDER "Benchmarks"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

# A real SDL_gpu Release-path benchmark. It is intentionally absent from
# CTest, install and stage: it opens a visible window and writes results only
# to stdout when invoked explicitly.
if (TARGET CKSdlGpuRasterizerStatic AND TARGET SDL3::SDL3)
    add_executable(sdl_gpu_pipeline_benchmark
        rasterizers/benchmark_sdl_gpu_pipeline.cpp)
    target_include_directories(sdl_gpu_pipeline_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKSdlGpuRasterizer
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKSdlGpuRasterizer/Native
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKSdlGpuRasterizer/Rasterizer
    )
    target_link_libraries(sdl_gpu_pipeline_benchmark PRIVATE
        CKFFPLib CKSdlGpuRasterizerStatic SDL3::SDL3)
    target_compile_definitions(sdl_gpu_pipeline_benchmark PRIVATE
        CKRE_BENCHMARK_RELEASE=$<IF:$<CONFIG:Release>,1,0>)
    set_target_properties(sdl_gpu_pipeline_benchmark PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Benchmarks"
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    ckre_copy_runtime_dependencies(sdl_gpu_pipeline_benchmark)
endif ()

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
    # Captures never depend on a compiled program manifest of earlier runs.
    set_tests_properties(scene_capture_golden PROPERTIES
        TIMEOUT 900
        SKIP_REGULAR_EXPRESSION "SKIPPED"
        ENVIRONMENT "CKRE_SDL_GPU_FF_JIT_CACHE=0"
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
        ENVIRONMENT "CKRE_SDL_GPU_FF_JIT_CACHE=0"
    )
endif ()

ckre_add_engine_test(nvstripifier_tests
    test_nvstripifier.cpp
)

ckre_add_engine_test(meshstriper_tests
    test_meshstriper.cpp
)

ckre_add_engine_test(geometry_regression_tests
    test_geometry_regressions.cpp
)

ckre_add_engine_test(scene_graph_tests
    test_scene_graph.cpp
)

ckre_add_engine_test(serialization_tests
    SerializationTest.cpp
)

# Optional integration with a locally built Grids plugin of the same architecture.
# The regular serialization suite does not require the BuildingBlocks project.
set(CKRE_GRID_TEST_PLUGIN "" CACHE FILEPATH "Grids plugin used by runtime grid serialization tests")
if (CKRE_GRID_TEST_PLUGIN)
    if (TARGET CK2Static)
        message(FATAL_ERROR "Runtime Grids tests require shared CK2. Configure the superproject with CK2_BUILD_STATIC=OFF.")
    endif ()
    add_test(NAME grid_serialization_tests
        COMMAND $<TARGET_FILE:serialization_tests> --grid-plugin "${CKRE_GRID_TEST_PLUGIN}")
    set_tests_properties(grid_serialization_tests PROPERTIES TIMEOUT 60)
endif ()

# Asset corpus is supplied locally, never bundled or required by CTest.
add_executable(serialization_corpus_tests SerializationCorpusTest.cpp)
ckre_configure_test_target(serialization_corpus_tests)
target_link_libraries(serialization_corpus_tests PRIVATE CK2_3DStatic)

ckre_add_engine_test(ck2d_entity_tests
    test_ck2d_entity.cpp
)

ckre_add_engine_test(ckmesh_tests
    test_ckmesh.cpp
)
target_compile_definitions(ckmesh_tests PRIVATE CKRE_ENABLE_TEST_ACCESS=1)

ckre_add_engine_recording_white_box_test(cktexture_tests
    test_cktexture.cpp
)

ckre_add_engine_test(simple_mesh_test
    simple_mesh_test.cpp
)

ckre_add_engine_recording_white_box_test(material_tests
    test_material.cpp
)

ckre_add_ckff_test(draw_state_cache_tests
    test_draw_state_cache.cpp
)

ckre_add_ckff_test(ff_uniform_state_tests
    test_ff_uniform_state.cpp
)

ckre_add_ckff_test(ffp_state_matrix_tests
    test_ffp_state_matrix.cpp
)

# The native fragment JIT against the uber shaders it specializes.
ckre_add_ckff_test(ff_native_fragment_jit_tests
    test_ff_native_fragment_jit.cpp
)
ckre_add_spirv_validation(FFNativeFragmentJitSpirvValidation ff_native_fragment_jit_tests ff_native_fragment_jit_spirv)
ckre_add_dxbc_validation(FFNativeFragmentJitDxbcValidation ff_native_fragment_jit_tests ff_native_fragment_jit_dxbc)

# Real-backend pixel gate: fixed-function semantics driven through the public
# rasterizer interface on the SDL_gpu plugin (env-gated, see test_rasterizer3_pixels.cpp).
if (TARGET CKSdlGpuRasterizerStatic AND TARGET SDL3::SDL3)
    ckre_add_recording_context_white_box_test(sdl_gpu_ffp_pixel_tests test_rasterizer3_pixels.cpp)
    target_include_directories(sdl_gpu_ffp_pixel_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKSdlGpuRasterizer/Rasterizer
    )
    target_compile_definitions(sdl_gpu_ffp_pixel_tests PRIVATE CKRE_PIXEL_SDL_GPU=1)
    target_link_libraries(sdl_gpu_ffp_pixel_tests PRIVATE CKSdlGpuRasterizerStatic SDL3::SDL3)
    # Software rasterizers on CI runners (lavapipe, WARP) take several minutes.
    set_tests_properties(sdl_gpu_ffp_pixel_tests PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif ()

ckre_add_recording_ckff_test(rasterizer_backend_tests
    test_rasterizer_backend.cpp
)

ckre_add_ckff_test(ff_state_store_tests
    test_ff_state_store.cpp
)

ckre_add_ckff_test(ff_state_resolver_tests
    test_ff_state_resolver.cpp
)

ckre_add_recording_ckff_test(ffp_pipeline_tests
    test_ffp_pipeline.cpp
)

ckre_add_engine_test(draw_annotation_tests
    test_draw_annotation.cpp
)
target_include_directories(draw_annotation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(draw_annotation_tests PRIVATE CKRasterizerLib)

ckre_add_ckff_test(render_settings_tests
    test_render_settings.cpp
)

if (TARGET CK2_3DStatic)
    ckre_add_engine_test(static_rasterizer_registration_tests
        test_static_rasterizer_registration.cpp
    )
    target_compile_definitions(static_rasterizer_registration_tests PRIVATE
        CKRE_EXPECT_SDL_GPU=$<BOOL:$<TARGET_EXISTS:CKSdlGpuRasterizerStatic>>)

endif ()

if (CKRE_ENABLE_FRAME_COST_STATS)
    ckre_add_rasterizer_library_test(render_frame_cost_stats_tests
        test_render_frame_cost_stats.cpp
    )
endif ()

ckre_add_engine_recording_white_box_test(render_transient_geometry_tests
    test_transient_geometry.cpp
)

add_executable(test_render_profile_disabled test_render_profile_disabled.cpp)
target_include_directories(test_render_profile_disabled PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib)
target_compile_definitions(test_render_profile_disabled PRIVATE CKRE_ENABLE_PROFILING=0)
set_target_properties(test_render_profile_disabled PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
add_test(NAME RenderProfileDisabled COMMAND $<TARGET_FILE:test_render_profile_disabled>)
