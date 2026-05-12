# CKRasterizer - rasterizer core and bgfx implementation
add_subdirectory(CKRasterizerLib)

set(CKBGFX_RASTERIZER_SOURCES
        CKBgfxRasterizer/CKBgfxCallback.cpp
        CKBgfxRasterizer/CKBgfxConfig.cpp
        CKBgfxRasterizer/CKBgfxInternal.cpp
        CKBgfxRasterizer/CKBgfxRasterizer.cpp
        CKBgfxRasterizer/CKBgfxRasterizerDriver.cpp
        CKBgfxRasterizer/CKBgfxRasterizerContext.cpp
)

set(CKBGFX_RASTERIZER_HEADERS
        CKBgfxRasterizer/CKBgfxConfig.h
        CKBgfxRasterizer/CKBgfxInternal.h
        CKBgfxRasterizer/CKBgfxRasterizer.h
)

set(CKBGFX_RASTERIZER_CONFIG
        ${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer/CKBgfxRasterizer.ini
)

set(_ckre_bgfx_targets)

if (CKRE_BUILD_SHARED)
    add_library(CKBgfxRasterizer SHARED ${CKBGFX_RASTERIZER_SOURCES} ${CKBGFX_RASTERIZER_HEADERS} ${CKBGFX_RASTERIZER_CONFIG})
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizer)

    if (MSVC)
        target_link_options(CKBgfxRasterizer PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:bgfx>)
    endif ()

    set_target_properties(CKBgfxRasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )

    add_custom_command(TARGET CKBgfxRasterizer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CKBGFX_RASTERIZER_CONFIG}"
            "$<TARGET_FILE_DIR:CKBgfxRasterizer>/CKBgfxRasterizer.ini"
            VERBATIM
    )
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKBgfxRasterizerStatic STATIC ${CKBGFX_RASTERIZER_SOURCES} ${CKBGFX_RASTERIZER_HEADERS} ${CKBGFX_RASTERIZER_CONFIG})
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizerStatic)

    set_target_properties(CKBgfxRasterizerStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )

    if (NOT TARGET CKBgfxRasterizer)
        add_library(CKBgfxRasterizer ALIAS CKBgfxRasterizerStatic)
    endif ()
endif ()

foreach (_ckre_bgfx_target IN LISTS _ckre_bgfx_targets)
    target_include_directories(${_ckre_bgfx_target}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer>
            $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer
    )

    target_link_libraries(${_ckre_bgfx_target}
            PRIVATE
            CKRasterizerLib
            bgfx
            bimg
            bx
    )
endforeach ()

# Tests
if (CKRE_BUILD_TESTS OR BUILD_TESTING)
    add_executable(test_null_rasterizer tests/test_null_rasterizer.cpp)
    target_include_directories(test_null_rasterizer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CKRE_INCLUDE_DIR}
    )
    target_link_libraries(test_null_rasterizer PRIVATE
            CKRasterizerLib
    )
    set_target_properties(test_null_rasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            FOLDER "Tests"
    )
    add_test(NAME NullRasterizerTests COMMAND test_null_rasterizer)

    add_executable(test_bgfx_rasterizer tests/test_bgfx_rasterizer.cpp)
    target_include_directories(test_bgfx_rasterizer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/CKBgfxRasterizer
            ${CKRE_INCLUDE_DIR}
    )
    if (TARGET CKBgfxRasterizerStatic)
        target_link_libraries(test_bgfx_rasterizer PRIVATE
                CKBgfxRasterizerStatic
                CKRasterizerLib
                bgfx
                bimg
                bx
        )
    else ()
        target_sources(test_bgfx_rasterizer PRIVATE ${CKBGFX_RASTERIZER_SOURCES} ${CKBGFX_RASTERIZER_HEADERS})
        target_link_libraries(test_bgfx_rasterizer PRIVATE
                CKRasterizerLib
                CK2
                VxMath
                bgfx
                bimg
                bx
        )
    endif ()
    set_target_properties(test_bgfx_rasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            FOLDER "Tests"
    )
    add_test(NAME BgfxRasterizerTests COMMAND test_bgfx_rasterizer)
endif ()

# Installation
if (CKRE_INSTALL)
    if (CKRE_BUILD_SHARED AND TARGET CKBgfxRasterizer)
        install(TARGETS CKBgfxRasterizer
                EXPORT CKRenderEngineTargets
                RUNTIME DESTINATION RenderEngines COMPONENT Runtime
                LIBRARY DESTINATION RenderEngines COMPONENT Runtime
                ARCHIVE DESTINATION lib COMPONENT Development
        )

        install(FILES ${CKBGFX_RASTERIZER_CONFIG}
                DESTINATION RenderEngines
                COMPONENT Runtime
        )
    endif ()

    if (TARGET CKBgfxRasterizerStatic)
        install(TARGETS CKBgfxRasterizerStatic
                ARCHIVE DESTINATION lib COMPONENT Development
        )
    endif ()
endif ()
