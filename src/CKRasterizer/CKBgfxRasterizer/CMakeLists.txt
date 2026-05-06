# CKBgfxRasterizer - bgfx-backed rasterizer implementation
set(CKBGFX_RASTERIZER_SOURCES
        CKBgfxRasterizer.cpp
        CKBgfxRasterizerDriver.cpp
        CKBgfxRasterizerContext.cpp
)

set(CKBGFX_RASTERIZER_HEADERS
        CKBgfxRasterizer.h
)

# Build bgfx as a dependency. shaderc is required by the CK2_3D shader target.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)

set(BGFX_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/bgfx")
if (NOT TARGET bgfx)
    add_subdirectory("${BGFX_DEPS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/bgfx-build")
endif ()

set(_ckre_bgfx_targets)

if (CKRE_BUILD_SHARED)
    add_library(CKBgfxRasterizer SHARED ${CKBGFX_RASTERIZER_SOURCES} ${CKBGFX_RASTERIZER_HEADERS})
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizer)

    set_target_properties(CKBgfxRasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )
endif ()

if (CKRE_BUILD_STATIC)
    add_library(CKBgfxRasterizerStatic STATIC ${CKBGFX_RASTERIZER_SOURCES} ${CKBGFX_RASTERIZER_HEADERS})
    list(APPEND _ckre_bgfx_targets CKBgfxRasterizerStatic)

    set_target_properties(CKBgfxRasterizerStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            FOLDER "Rasterizer"
    )
endif ()

foreach (_ckre_bgfx_target IN LISTS _ckre_bgfx_targets)
    target_include_directories(${_ckre_bgfx_target}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(${_ckre_bgfx_target}
            PRIVATE
            CK2
            VxMath
            bgfx
            bimg
            bx
    )
endforeach ()

# Tests
if (CKRE_BUILD_TESTS OR BUILD_TESTING)
    add_executable(test_bgfx_rasterizer tests/test_bgfx_rasterizer.cpp)
    target_include_directories(test_bgfx_rasterizer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CKRE_INCLUDE_DIR}
    )
    target_link_libraries(test_bgfx_rasterizer PRIVATE
            CK2
            VxMath
            bgfx
            bimg
            bx
    )
    set_target_properties(test_bgfx_rasterizer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            FOLDER "Tests"
    )
    add_test(NAME BgfxRasterizerTests COMMAND test_bgfx_rasterizer)
endif ()

# Installation
if (CKRE_INSTALL)
    if (TARGET CKBgfxRasterizer)
        install(TARGETS CKBgfxRasterizer
                EXPORT CKRenderEngineTargets
                RUNTIME DESTINATION RenderEngines
                LIBRARY DESTINATION RenderEngines
                ARCHIVE DESTINATION lib
        )
    endif ()

    if (TARGET CKBgfxRasterizerStatic)
        install(TARGETS CKBgfxRasterizerStatic
                ARCHIVE DESTINATION lib
        )
    endif ()
endif ()
