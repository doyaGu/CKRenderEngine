# RESOLVE_AND_STORE and GPU device properties are part of SDL 3.4.
find_package(SDL3 3.4 CONFIG REQUIRED)

set(CKSDL_GPU_RASTERIZER_SOURCES
    Backend/CKSdlGpuBackend.cpp
    Backend/CKSdlGpuCommands.cpp
    Backend/CKSdlGpuMipmaps.cpp
    Backend/CKSdlGpuNativeShaders.cpp
    Backend/CKSdlGpuPipeline.cpp
    Backend/CKSdlGpuResources.cpp
    Backend/CKSdlGpuTextureData.cpp
    Provider/CKSdlGpuRasterizer.cpp
    Provider/CKSdlGpuShaders.cpp
)
set(CKSDL_GPU_RASTERIZER_HEADERS
    Backend/CKSdlGpuBackend.h
    Backend/CKSdlGpuInternal.h
    Backend/CKSdlGpuNativeShaders.h
    Backend/CKSdlGpuTextureData.h
    Backend/CKSdlGpuUniformBatch.h
    Provider/CKSdlGpuShaders.h
)

function(ckre_configure_sdl_gpu_rasterizer TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Backend
        ${CMAKE_CURRENT_SOURCE_DIR}/Provider
    )
    target_link_libraries(${TARGET_NAME} PRIVATE CKFFPLib SDL3::SDL3)
    set_target_properties(${TARGET_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endfunction()

set(_cksdl_targets)
if (CKRE_BUILD_SHARED)
    add_library(CKSdlGpuRasterizer SHARED
        ${CKSDL_GPU_RASTERIZER_SOURCES}
        ${CKSDL_GPU_RASTERIZER_HEADERS}
    )
    ckre_configure_sdl_gpu_rasterizer(CKSdlGpuRasterizer)
    set_target_properties(CKSdlGpuRasterizer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    list(APPEND _cksdl_targets CKSdlGpuRasterizer)
    if (CKRE_INSTALL)
        install(TARGETS CKSdlGpuRasterizer
            RUNTIME DESTINATION RenderEngines COMPONENT Runtime
            LIBRARY DESTINATION RenderEngines COMPONENT Runtime
        )
    endif ()
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKSdlGpuRasterizerStatic STATIC
        ${CKSDL_GPU_RASTERIZER_SOURCES}
        ${CKSDL_GPU_RASTERIZER_HEADERS}
    )
    target_compile_definitions(CKSdlGpuRasterizerStatic PRIVATE CK_LIB=1)
    ckre_configure_sdl_gpu_rasterizer(CKSdlGpuRasterizerStatic)
    list(APPEND _cksdl_targets CKSdlGpuRasterizerStatic)
    if (NOT TARGET CKSdlGpuRasterizer)
        add_library(CKSdlGpuRasterizer ALIAS CKSdlGpuRasterizerStatic)
    endif ()
endif ()

if (CKRE_BUILD_TESTS AND BUILD_TESTING)
    foreach (_test test_sdl_gpu_rasterizer test_sdl_gpu_pixels)
        add_executable(${_test}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/providers/${_test}.cpp)
        target_include_directories(${_test} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/Backend
            ${CMAKE_CURRENT_SOURCE_DIR}/Provider
            ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/FixedFunction
            ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/Context
        )
        target_link_libraries(${_test} PRIVATE CKFFPLib SDL3::SDL3)
        if (TARGET CKSdlGpuRasterizerStatic)
            target_link_libraries(${_test} PRIVATE CKSdlGpuRasterizerStatic)
        else ()
            target_sources(${_test} PRIVATE
                ${CKSDL_GPU_RASTERIZER_SOURCES}
                ${CKSDL_GPU_RASTERIZER_HEADERS}
            )
            target_compile_definitions(${_test} PRIVATE CK_LIB=1)
        endif ()
        set_target_properties(${_test} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            FOLDER "Tests"
        )
        ckre_copy_runtime_dependencies(${_test})
    endforeach ()
    add_test(NAME SdlGpuRasterizerTests COMMAND $<TARGET_FILE:test_sdl_gpu_rasterizer>)
    set_tests_properties(SdlGpuRasterizerTests PROPERTIES TIMEOUT 60)
    add_test(NAME SdlGpuPixelTests COMMAND $<TARGET_FILE:test_sdl_gpu_pixels>)
    set_tests_properties(SdlGpuPixelTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif ()

# Checked-in native payloads are sufficient at runtime and in ordinary builds.
# Verify source/ABI ownership without invoking a shader compiler.
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_custom_target(CKSdlGpuShaderArtifacts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_native_shaders.py --verify
        VERBATIM
    )
    foreach (_target IN LISTS _cksdl_targets)
        add_dependencies(${_target} CKSdlGpuShaderArtifacts)
    endforeach ()
    if (CKRE_BUILD_TESTS AND BUILD_TESTING)
        add_test(NAME SdlGpuShaderArtifacts
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_native_shaders.py --verify
        )
    endif ()
endif ()
