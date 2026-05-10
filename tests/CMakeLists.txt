function(ckre_add_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CKRE_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CKRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
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
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

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

ckre_add_test(ckmesh_tests
    test_ckmesh.cpp
)

ckre_add_test(simple_mesh_test
    simple_mesh_test.cpp
)
