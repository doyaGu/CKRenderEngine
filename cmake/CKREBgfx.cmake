include_guard(GLOBAL)

# Keep upstream sources immutable and outside the source tree. All four archives
# belong to the same upstream bgfx.cmake revision; no git submodule or patch step.
function(ckre_fetch_bgfx_sources)
    if(NOT CKRE_BUILD_BGFX_RASTERIZER)
        return()
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.20)
        message(FATAL_ERROR "The optional bgfx build requires CMake 3.20 or newer")
    endif()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    FetchContent_Declare(ckre_bgfx_cmake
        URL https://codeload.github.com/bkaradzic/bgfx.cmake/tar.gz/bc2f286a7ca98d435c7f3c3c43d8258246b17918
        URL_HASH SHA256=375fc9d5811690381154e036673a3127725a71e371c32d14e0ede61558df8ef0
        SOURCE_SUBDIR .ckre-sources-only)
    FetchContent_Declare(ckre_bgfx
        URL https://codeload.github.com/bkaradzic/bgfx/tar.gz/c7684e20da1e385edc439ef39cdb42b8c661016f
        URL_HASH SHA256=2c473c1eae44ecc43f16e601c5228735c36c59dc0eaaf59e9faa0c703c6a4304
        SOURCE_SUBDIR .ckre-sources-only)
    FetchContent_Declare(ckre_bx
        URL https://codeload.github.com/bkaradzic/bx/tar.gz/0b001f5f36579e8aea07efa5af139ca18dad9505
        URL_HASH SHA256=6838db5c4d78d2e901f3828ab7d4ee143f5c701a0ee6e1b7ab1f72a4fea12a6a
        SOURCE_SUBDIR .ckre-sources-only)
    FetchContent_Declare(ckre_bimg
        URL https://codeload.github.com/bkaradzic/bimg/tar.gz/3b4baab0128ac499c5c3bc37202781bf54084049
        URL_HASH SHA256=e8cdb1ab4e17388cd560194d8372da4464fa72434d432fdcbae56be246d68a53
        SOURCE_SUBDIR .ckre-sources-only)
    FetchContent_MakeAvailable(ckre_bgfx_cmake ckre_bgfx ckre_bx ckre_bimg)
    set(CKRE_BGFX_CMAKE_SOURCE_DIR "${ckre_bgfx_cmake_SOURCE_DIR}" PARENT_SCOPE)
    set(BGFX_DIR "${ckre_bgfx_SOURCE_DIR}" PARENT_SCOPE)
    set(BX_DIR "${ckre_bx_SOURCE_DIR}" PARENT_SCOPE)
    set(BIMG_DIR "${ckre_bimg_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
