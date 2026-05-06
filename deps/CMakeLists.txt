# RenderEngine third-party dependencies

# bgfx is the default rasterizer backend. Build shaderc too because CK2_3D
# generates fixed-function shader blobs as part of the normal build.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)

if (NOT TARGET bgfx)
    add_subdirectory(bgfx)
endif ()
