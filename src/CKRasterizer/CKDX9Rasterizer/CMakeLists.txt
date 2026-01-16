# CKDX9Rasterizer - DirectX 9 rasterizer implementation
set(CKDX9_RASTERIZER_SOURCES
        CKDX9Rasterizer.cpp
        CKDX9RasterizerDriver.cpp
        CKDX9RasterizerContext.cpp
)

set(CKDX9_RASTERIZER_HEADERS
        CKDX9Rasterizer.h
)

set(_ckre_dx9_targets)

if (CKRE_BUILD_SHARED)
    add_library(CKDX9Rasterizer SHARED ${CKDX9_RASTERIZER_SOURCES} ${CKDX9_RASTERIZER_HEADERS})
    list(APPEND _ckre_dx9_targets CKDX9Rasterizer)

    set_target_properties(CKDX9Rasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKDX9RasterizerStatic STATIC ${CKDX9_RASTERIZER_SOURCES} ${CKDX9_RASTERIZER_HEADERS})
    list(APPEND _ckre_dx9_targets CKDX9RasterizerStatic)

    set_target_properties(CKDX9RasterizerStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )
endif ()

foreach (_ckre_dx9_target IN LISTS _ckre_dx9_targets)
    target_include_directories(${_ckre_dx9_target}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Link against CKRasterizerLib and Virtools SDK
    target_link_libraries(${_ckre_dx9_target}
            PUBLIC
            CKRasterizerLib
            PRIVATE
            CK2
            VxMath
            d3d9 dxguid
    )
endforeach ()

# =============================================================================
# Installation
# =============================================================================
if (CKRE_INSTALL)
    if (TARGET CKDX9Rasterizer)
        install(TARGETS CKDX9Rasterizer
                EXPORT CKRenderEngineTargets
                RUNTIME DESTINATION RenderEngines
                LIBRARY DESTINATION RenderEngines
                ARCHIVE DESTINATION lib
        )
    endif ()

    if (TARGET CKDX9RasterizerStatic)
        install(TARGETS CKDX9RasterizerStatic
                EXPORT CKRenderEngineTargets
                ARCHIVE DESTINATION lib
        )
    endif ()
endif ()
