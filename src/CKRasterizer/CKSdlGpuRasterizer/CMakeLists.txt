add_library(CKSdlGpuRasterizerSupport STATIC
        CKSdlGpuShaderTarget.cpp
        CKSdlGpuShaderTarget.h
)
target_compile_features(CKSdlGpuRasterizerSupport PRIVATE cxx_std_20)
target_include_directories(CKSdlGpuRasterizerSupport
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../CKRasterizerLib
)
target_link_libraries(CKSdlGpuRasterizerSupport
        PUBLIC
        CKRasterizerLib
        SDL3::SDL3
)
set_target_properties(CKSdlGpuRasterizerSupport PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
)

if (CKRE_BUILD_TESTS)
    add_executable(test_sdl_gpu_support ../tests/test_sdl_gpu_support.cpp)
    target_link_libraries(test_sdl_gpu_support PRIVATE CKSdlGpuRasterizerSupport)
    set_target_properties(test_sdl_gpu_support PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            FOLDER "Tests"
    )
    ckre_copy_runtime_dependencies(test_sdl_gpu_support)
    add_test(NAME SdlGpuSupportTests COMMAND $<TARGET_FILE:test_sdl_gpu_support>)
    set_tests_properties(SdlGpuSupportTests PROPERTIES TIMEOUT 60)
endif ()
