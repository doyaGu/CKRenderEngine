function(ckre_configure_test_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CKRE_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
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

# Tests of the CKRasterizer v3 contract: the translation core (CKRasterizerLib)
# driven through the public contract only, over the recording device of
# FFPRecordingHarness.h. They do not link the engine.
function(ckre_add_rasterizer3_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKRasterizerLib
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CKRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    target_link_libraries(${TARGET_NAME} PRIVATE CKRasterizerLib)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "Tests"
    )
    ckre_copy_runtime_dependencies(${TARGET_NAME})
    add_test(NAME ${TARGET_NAME} COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT 60)
endfunction()

ckre_add_rasterizer3_test(rasterizer3_contract_tests
    test_rasterizer3_contract.cpp
)

# Translation core internals (state forwarding into the fixed-function
# pipeline, device calls) on the same recording device.
ckre_add_rasterizer3_test(rasterizer3_translated_tests
    test_rasterizer3_translated.cpp
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

ckre_add_test(ffp_contract_exhaustive_tests
    test_ffp_contract_exhaustive.cpp
)

# Real-backend pixel gate: the fixed-function semantics driven through the v3
# contract on the bgfx plugin (env-gated, see test_rasterizer3_pixels.cpp).
if (TARGET CKBgfxRasterizerStatic AND TARGET SDL3::SDL3)
    ckre_add_rasterizer3_test(rasterizer3_pixel_tests
        test_rasterizer3_pixels.cpp
    )
    target_include_directories(rasterizer3_pixel_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/CKRasterizer/CKBgfxRasterizer
    )
    target_link_libraries(rasterizer3_pixel_tests PRIVATE
        CKBgfxRasterizerStatic
        SDL3::SDL3
        bgfx
        bimg
        bx
    )
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

if (TARGET CKBgfxRasterizerStatic)
    ckre_add_test(static_rasterizer_registration_tests
        test_static_rasterizer_registration.cpp
    )
endif ()

if (CKRE_ENABLE_FRAME_COST_STATS)
    ckre_add_test(render_frame_cost_stats_tests
        test_render_frame_cost_stats.cpp
    )
endif ()

ckre_add_test(render_transient_geometry_tests
    test_transient_geometry.cpp
)
