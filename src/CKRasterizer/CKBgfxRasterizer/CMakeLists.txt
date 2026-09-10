set(CKBGFX_RASTERIZER_SOURCES
    CKBgfxBackend.cpp
    CKBgfxBackendDriver.cpp
    CKBgfxCallback.cpp
    CKBgfxClear.cpp
    CKBgfxConfig.cpp
    CKBgfxDrawMapTrace.cpp
    CKBgfxInternal.cpp
    CKBgfxRasterizer.cpp
    CKBgfxShaders.cpp
)
set(CKBGFX_RASTERIZER_HEADERS
    CKBgfxBackend.h
    CKBgfxBorderPalette.h
    CKBgfxConfig.h
    CKBgfxDrawMapTrace.h
    CKBgfxInternal.h
    CKBgfxRasterizer.h
    CKBgfxResources.h
)
set(CKBGFX_RASTERIZER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer.ini")

if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach (_target bx bimg bgfx)
        if (TARGET ${_target})
            set_target_properties(${_target} PROPERTIES
                CXX_COMPILER_LAUNCHER "/bin/sh;${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxAppleClangLauncher.sh"
            )
            target_link_libraries(${_target} PUBLIC c++)
        endif ()
    endforeach ()
endif ()

function(ckre_configure_bgfx_rasterizer TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        CKFFPLib
        bgfx
        bimg
        bx
        SDL3::SDL3
    )
    if (CMAKE_DL_LIBS)
        target_link_libraries(${TARGET_NAME} PRIVATE ${CMAKE_DL_LIBS})
    endif ()
    if (TARGET CK2_3D_shaders)
        add_dependencies(${TARGET_NAME} CK2_3D_shaders)
    endif ()
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
    )
endfunction()

if (CKRE_BUILD_SHARED)
    set(_ckbgfx_version_resource)
    if (WIN32)
        set(CKRE_RC_TARGET CKBgfxRasterizer)
        set(CKRE_RC_DESCRIPTION "bgfx Rasterizer")
        set(CKRE_RC_ORIGINAL_FILENAME "CKBgfxRasterizer.dll")
        set(_ckbgfx_version_resource "${CMAKE_CURRENT_BINARY_DIR}/CKBgfxRasterizerVersion.generated.rc")
        configure_file("${PROJECT_SOURCE_DIR}/src/Version.rc.in" "${_ckbgfx_version_resource}" @ONLY)
    endif ()
    add_library(CKBgfxRasterizer SHARED
        ${CKBGFX_RASTERIZER_SOURCES}
        ${CKBGFX_RASTERIZER_HEADERS}
        ${CKBGFX_RASTERIZER_CONFIG}
        ${_ckbgfx_version_resource}
    )
    ckre_configure_bgfx_rasterizer(CKBgfxRasterizer)
    if (MSVC)
        target_link_options(CKBgfxRasterizer PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:bgfx>)
    endif ()
    add_custom_command(TARGET CKBgfxRasterizer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CKBGFX_RASTERIZER_CONFIG}"
            "$<TARGET_FILE_DIR:CKBgfxRasterizer>/CKBgfxRasterizer.ini"
        VERBATIM
    )
    if (CKRE_INSTALL)
        install(TARGETS CKBgfxRasterizer
            RUNTIME DESTINATION RenderEngines COMPONENT Runtime
            LIBRARY DESTINATION RenderEngines COMPONENT Runtime
        )
        install(FILES ${CKBGFX_RASTERIZER_CONFIG}
            DESTINATION RenderEngines
            COMPONENT Runtime
        )
    endif ()
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKBgfxRasterizerStatic STATIC
        ${CKBGFX_RASTERIZER_SOURCES}
        ${CKBGFX_RASTERIZER_HEADERS}
        ${CKBGFX_RASTERIZER_CONFIG}
    )
    target_compile_definitions(CKBgfxRasterizerStatic PRIVATE CK_LIB=1)
    ckre_configure_bgfx_rasterizer(CKBgfxRasterizerStatic)
    if (NOT TARGET CKBgfxRasterizer)
        add_library(CKBgfxRasterizer ALIAS CKBgfxRasterizerStatic)
    endif ()
endif ()

if (CKRE_BUILD_TESTS AND BUILD_TESTING)
    add_executable(test_bgfx_rasterizer ../tests/test_bgfx_rasterizer.cpp)
    target_include_directories(test_bgfx_rasterizer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib
    )
    target_link_libraries(test_bgfx_rasterizer PRIVATE CKFFPLib bgfx bimg bx SDL3::SDL3)
    if (TARGET CKBgfxRasterizerStatic)
        target_link_libraries(test_bgfx_rasterizer PRIVATE CKBgfxRasterizerStatic)
    else ()
        target_sources(test_bgfx_rasterizer PRIVATE
            ${CKBGFX_RASTERIZER_SOURCES}
            ${CKBGFX_RASTERIZER_HEADERS}
        )
        target_compile_definitions(test_bgfx_rasterizer PRIVATE CK_LIB=1)
    endif ()
    set_target_properties(test_bgfx_rasterizer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Tests"
    )
    ckre_copy_runtime_dependencies(test_bgfx_rasterizer)
    add_test(NAME BgfxRasterizerTests COMMAND $<TARGET_FILE:test_bgfx_rasterizer>)
    set_tests_properties(BgfxRasterizerTests PROPERTIES TIMEOUT 60)
endif ()
