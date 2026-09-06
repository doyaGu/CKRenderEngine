# RESOLVE_AND_STORE and GPU device properties are part of SDL 3.4.
find_package(SDL3 3.4 CONFIG REQUIRED)

set(CKSDL_GPU_BACKEND_SOURCES
        CKSdlGpuBackend.cpp
        CKSdlGpuResources.cpp
        CKSdlGpuTextureData.cpp
        CKSdlGpuMipmaps.cpp
        CKSdlGpuPipeline.cpp
        CKSdlGpuCommands.cpp
        CKSdlGpuNativeShaders.cpp
)
add_library(CKSdlGpuBackend STATIC ${CKSDL_GPU_BACKEND_SOURCES}
        CKSdlGpuBackend.h CKSdlGpuInternal.h CKSdlGpuNativeShaders.h CKSdlGpuTextureData.h)
target_compile_features(CKSdlGpuBackend PRIVATE cxx_std_20)
target_include_directories(CKSdlGpuBackend PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(CKSdlGpuBackend PUBLIC CKRasterizerBackend SDL3::SDL3)
target_link_libraries(CKSdlGpuBackend PRIVATE CKRenderProfiling)
set_target_properties(CKSdlGpuBackend PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" FOLDER "Rasterizer/Backend"
        POSITION_INDEPENDENT_CODE ON)
if (CKRE_INSTALL)
    install(TARGETS CKSdlGpuBackend ARCHIVE DESTINATION lib COMPONENT Development)
endif ()

set(CKSDL_GPU_RASTERIZER_SOURCES CKSdlGpuShaders.cpp CKSdlGpuRasterizer.cpp)
set(_cksdl_targets)
if (CKRE_BUILD_SHARED)
    add_library(CKSdlGpuRasterizer SHARED ${CKSDL_GPU_RASTERIZER_SOURCES})
    list(APPEND _cksdl_targets CKSdlGpuRasterizer)
    set_target_properties(CKSdlGpuRasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    if (CKRE_INSTALL)
        install(TARGETS CKSdlGpuRasterizer
                RUNTIME DESTINATION RenderEngines COMPONENT Runtime
                LIBRARY DESTINATION RenderEngines COMPONENT Runtime
                ARCHIVE DESTINATION lib COMPONENT Development)
    endif ()
endif ()
if (CKRE_BUILD_STATIC)
    add_library(CKSdlGpuRasterizerStatic STATIC ${CKSDL_GPU_RASTERIZER_SOURCES})
    target_compile_definitions(CKSdlGpuRasterizerStatic PRIVATE CK_LIB=1)
    list(APPEND _cksdl_targets CKSdlGpuRasterizerStatic)
    if (NOT TARGET CKSdlGpuRasterizer)
        add_library(CKSdlGpuRasterizer ALIAS CKSdlGpuRasterizerStatic)
    endif ()
    if (CKRE_INSTALL)
        install(TARGETS CKSdlGpuRasterizerStatic ARCHIVE DESTINATION lib COMPONENT Development)
    endif ()
endif ()
foreach (_target IN LISTS _cksdl_targets)
    target_compile_features(${_target} PRIVATE cxx_std_20)
    target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../CKRasterizerLib)
    target_link_libraries(${_target} PRIVATE CKSdlGpuBackend CKRasterizerLib)
    set_target_properties(${_target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" FOLDER "Rasterizer")
endforeach ()

if (CKRE_BUILD_TESTS AND BUILD_TESTING)
    add_executable(test_sdl_gpu_rasterizer ../tests/test_sdl_gpu_rasterizer.cpp)
    target_link_libraries(test_sdl_gpu_rasterizer PRIVATE CKSdlGpuBackend CKRasterizerLib)
    set_target_properties(test_sdl_gpu_rasterizer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
    ckre_copy_runtime_dependencies(test_sdl_gpu_rasterizer)
    add_test(NAME SdlGpuRasterizerTests COMMAND $<TARGET_FILE:test_sdl_gpu_rasterizer>)
    set_tests_properties(SdlGpuRasterizerTests PROPERTIES TIMEOUT 60)
    add_executable(test_sdl_gpu_pixels ../tests/test_sdl_gpu_pixels.cpp)
    target_link_libraries(test_sdl_gpu_pixels PRIVATE CKSdlGpuBackend CKRasterizerLib)
    foreach (_test test_sdl_gpu_rasterizer test_sdl_gpu_pixels)
        target_include_directories(${_test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../CKRasterizerLib)
        if (TARGET CKSdlGpuRasterizerStatic)
            target_link_libraries(${_test} PRIVATE CKSdlGpuRasterizerStatic)
        else ()
            target_sources(${_test} PRIVATE ${CKSDL_GPU_RASTERIZER_SOURCES})
        endif ()
    endforeach ()
    set_target_properties(test_sdl_gpu_pixels PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
    ckre_copy_runtime_dependencies(test_sdl_gpu_pixels)
    add_test(NAME SdlGpuPixelTests COMMAND $<TARGET_FILE:test_sdl_gpu_pixels>)
    set_tests_properties(SdlGpuPixelTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif ()

# Checked-in native payloads are sufficient at runtime and in ordinary builds.
# Verify source/ABI ownership without invoking a shader compiler.
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_custom_target(CKSdlGpuShaderArtifacts
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_native_shaders.py --verify
        VERBATIM)
    add_dependencies(CKSdlGpuBackend CKSdlGpuShaderArtifacts)
    foreach (_target IN LISTS _cksdl_targets)
        add_dependencies(${_target} CKSdlGpuShaderArtifacts)
    endforeach ()
    if (CKRE_BUILD_TESTS AND BUILD_TESTING)
        add_test(NAME SdlGpuShaderArtifacts
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compile_native_shaders.py --verify)
    endif ()
endif ()
