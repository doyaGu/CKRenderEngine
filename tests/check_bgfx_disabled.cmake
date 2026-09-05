# A disabled backend must return before even loading FetchContent. In script
# mode MakeAvailable would fail, so this also catches accidental unconditional use.
set(CKRE_BUILD_BGFX_RASTERIZER OFF)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CKREBgfx.cmake")
ckre_fetch_bgfx_sources()
if(COMMAND FetchContent_Declare OR DEFINED BGFX_DIR OR DEFINED CKRE_BGFX_CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Disabled bgfx initialized dependency machinery")
endif()
