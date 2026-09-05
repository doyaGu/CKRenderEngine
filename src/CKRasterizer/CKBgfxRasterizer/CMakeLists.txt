# Native device implementation. It consumes generic commands and owns no FFP catalog.
set(CKBGFX_BACKEND_SOURCES
        CKBgfxBackend.cpp
        CKBgfxCallback.cpp
        CKBgfxConfig.cpp
        CKBgfxDrawMapTrace.cpp
        CKBgfxInternal.cpp
)
set(CKBGFX_BACKEND_HEADERS
        CKBgfxBackend.h CKBgfxResources.h CKBgfxConfig.h CKBgfxDrawMapTrace.h
        CKBgfxInternal.h CKBgfxBorderPalette.h
)
add_library(CKBgfxBackend STATIC ${CKBGFX_BACKEND_SOURCES} ${CKBGFX_BACKEND_HEADERS})
target_compile_features(CKBgfxBackend PRIVATE cxx_std_20)
target_include_directories(CKBgfxBackend PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(CKBgfxBackend PUBLIC CKRasterizerBackend CKRenderSupport bgfx bimg bx SDL3::SDL3)
if (CMAKE_DL_LIBS)
    target_link_libraries(CKBgfxBackend PRIVATE ${CMAKE_DL_LIBS})
endif ()
set_target_properties(CKBgfxBackend PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" FOLDER "Rasterizer/Backend"
        POSITION_INDEPENDENT_CODE ON)

if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach (_target bx bimg bgfx)
        if (TARGET ${_target})
            set_target_properties(${_target} PROPERTIES
                    CXX_COMPILER_LAUNCHER "/bin/sh;${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxAppleClangLauncher.sh")
            target_link_libraries(${_target} PUBLIC c++)
        endif ()
    endforeach ()
endif ()

# Composition is the only target that binds the native device to FFP artifacts.
set(CKBGFX_RASTERIZER_SOURCES CKBgfxShaders.cpp CKBgfxBackendDriver.cpp CKBgfxRasterizer.cpp)
set(CKBGFX_RASTERIZER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer.ini")
set(_ckre_bgfx_targets)
if (CKRE_BUILD_SHARED)
    set(_ckbgfx_version_resource)
    if (WIN32)
        set(CKRE_RC_TARGET CKBgfxRasterizer)
        set(CKRE_RC_DESCRIPTION "bgfx Rasterizer")
        set(CKRE_RC_ORIGINAL_FILENAME "CKBgfxRasterizer.dll")
        set(_ckbgfx_version_resource "${CMAKE_CURRENT_BINARY_DIR}/CKBgfxRasterizerVersion.generated.rc")
        configure_file("${PROJECT_SOURCE_DIR}/src/Version.rc.in" "${_ckbgfx_version_resource}" @ONLY)
    endif ()
    add_library(CKBgfxRasterizer SHARED ${CKBGFX_RASTERIZER_SOURCES} CKBgfxRasterizer.h
            ${CKBGFX_RASTERIZER_CONFIG} ${_ckbgfx_version_resource})
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizer)
    if (MSVC)
        target_link_options(CKBgfxRasterizer PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:bgfx>)
    endif ()
    add_custom_command(TARGET CKBgfxRasterizer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CKBGFX_RASTERIZER_CONFIG}"
                    "$<TARGET_FILE_DIR:CKBgfxRasterizer>/CKBgfxRasterizer.ini" VERBATIM)
endif ()
if (CKRE_BUILD_STATIC)
    add_library(CKBgfxRasterizerStatic STATIC ${CKBGFX_RASTERIZER_SOURCES} CKBgfxRasterizer.h ${CKBGFX_RASTERIZER_CONFIG})
    target_compile_definitions(CKBgfxRasterizerStatic PRIVATE CK_LIB=1)
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizerStatic)
    if (NOT TARGET CKBgfxRasterizer)
        add_library(CKBgfxRasterizer ALIAS CKBgfxRasterizerStatic)
    endif ()
endif ()
foreach (_target IN LISTS _ckre_bgfx_targets)
    target_compile_features(${_target} PRIVATE cxx_std_20)
    target_include_directories(${_target} PUBLIC
            $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../CKRasterizerLib)
    target_link_libraries(${_target} PRIVATE CKBgfxBackend CKRasterizerLib)
    if (TARGET CK2_3D_shaders)
        add_dependencies(${_target} CK2_3D_shaders)
    endif ()
    set_target_properties(${_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" FOLDER "Rasterizer")
endforeach ()

if (CKRE_BUILD_TESTS AND BUILD_TESTING)
    add_executable(test_bgfx_rasterizer ../tests/test_bgfx_rasterizer.cpp)
    target_include_directories(test_bgfx_rasterizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../CKRasterizerLib)
    target_link_libraries(test_bgfx_rasterizer PRIVATE CKBgfxBackend CKRasterizerLib)
    if (TARGET CKBgfxRasterizerStatic)
        target_link_libraries(test_bgfx_rasterizer PRIVATE CKBgfxRasterizerStatic)
    else ()
        target_sources(test_bgfx_rasterizer PRIVATE ${CKBGFX_RASTERIZER_SOURCES})
    endif ()
    set_target_properties(test_bgfx_rasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" FOLDER "Tests")
    ckre_copy_runtime_dependencies(test_bgfx_rasterizer)
    add_test(NAME BgfxRasterizerTests COMMAND $<TARGET_FILE:test_bgfx_rasterizer>)
    set_tests_properties(BgfxRasterizerTests PROPERTIES TIMEOUT 60)
endif ()

if (CKRE_INSTALL)
    install(TARGETS CKBgfxBackend ARCHIVE DESTINATION lib COMPONENT Development)
    if (CKRE_BUILD_SHARED)
        install(TARGETS CKBgfxRasterizer EXPORT CKRenderEngineTargets
                RUNTIME DESTINATION RenderEngines COMPONENT Runtime
                LIBRARY DESTINATION RenderEngines COMPONENT Runtime
                ARCHIVE DESTINATION lib COMPONENT Development)
        install(FILES ${CKBGFX_RASTERIZER_CONFIG} DESTINATION RenderEngines COMPONENT Runtime)
    endif ()
    if (TARGET CKBgfxRasterizerStatic)
        install(TARGETS CKBgfxRasterizerStatic ARCHIVE DESTINATION lib COMPONENT Development)
    endif ()
endif ()
