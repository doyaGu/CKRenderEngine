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

    if (NOT TARGET CKDX9Rasterizer)
        add_library(CKDX9Rasterizer ALIAS CKDX9RasterizerStatic)
    endif ()
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

    get_target_property(_ckre_dx9_type ${_ckre_dx9_target} TYPE)
    if (_ckre_dx9_type STREQUAL "STATIC_LIBRARY")
        set(_ckre_dx9_ck2_dep CK2)
        if (TARGET CK2Static)
            set(_ckre_dx9_ck2_dep CK2Static)
        endif ()
        set(_ckre_dx9_vxmath_dep VxMath)
        if (TARGET VxMathStatic)
            set(_ckre_dx9_vxmath_dep VxMathStatic)
        endif ()
    else ()
        set(_ckre_dx9_ck2_dep CK2)
        set(_ckre_dx9_vxmath_dep VxMath)
    endif ()

    target_link_libraries(${_ckre_dx9_target}
            PUBLIC
            CKRasterizerLib
            PRIVATE
            ${_ckre_dx9_ck2_dep}
            ${_ckre_dx9_vxmath_dep}
            d3d9 dxguid
    )
endforeach ()

# =============================================================================
# Installation
# =============================================================================
if (CKRE_INSTALL)
    if (CKRE_BUILD_SHARED AND TARGET CKDX9Rasterizer)
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
