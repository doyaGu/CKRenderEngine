# RenderEngine third-party dependencies

# bgfx is the default rasterizer backend. Build target-architecture bgfx tools
# only when CK2_3D can run them on this host. Cross builds use CKREHostShaderc.
set(BGFX_BUILD_TOOLS ${CKRE_BUILD_TARGET_SHADERC} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ${CKRE_BUILD_TARGET_SHADERC} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)

if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
else ()
    set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)
endif ()

if (NOT TARGET bgfx)
    if (MSVC)
        # bx must compile without C++ unwinding because its Windows thread-name
        # fallback uses SEH __try.  Remove the inherited /EHsc while creating
        # the third-party targets; the non-bx targets restore it below.
        set(_ckre_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "(^| )/EHsc( |$)" " " CMAKE_CXX_FLAGS
                "${CMAKE_CXX_FLAGS}")
    endif ()

    add_subdirectory(bgfx)

    if (MSVC)
        set(CMAKE_CXX_FLAGS "${_ckre_saved_cxx_flags}")
    endif ()
endif ()

# bimg's decoder includes miniz.c directly from image_decode.cpp.  The bundled
# bgfx CMake wrapper also adds miniz.c as a separate source, which produces a
# duplicate implementation in bimg_decode and LNK4006 for every exported miniz
# symbol on MSVC.
if (TARGET bimg_decode)
    get_target_property(_ckre_bimg_decode_sources bimg_decode SOURCES)
    if (_ckre_bimg_decode_sources)
        list(FILTER _ckre_bimg_decode_sources EXCLUDE
                REGEX "[/\\\\]tinyexr[/\\\\]deps[/\\\\]miniz[/\\\\]miniz\\.c$")
        set_property(TARGET bimg_decode PROPERTY SOURCES
                "${_ckre_bimg_decode_sources}")
    endif ()
endif ()

if (TARGET bimg_encode AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE
            "${BIMG_DIR}/3rdparty/etcpak/ProcessRGB.cpp"
            TARGET_DIRECTORY bimg_encode APPEND PROPERTY
            COMPILE_OPTIONS
            "-Wno-logical-not-parentheses;-Wno-macro-redefined;-Wno-return-type")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        set(_ckre_nvtt_memcall_warning "-Wno-nontrivial-memaccess")
    else ()
        set(_ckre_nvtt_memcall_warning "-Wno-nontrivial-memcall")
    endif ()
    set_property(SOURCE
            "${BIMG_DIR}/3rdparty/nvtt/nvmath/fitting.cpp"
            TARGET_DIRECTORY bimg_encode APPEND PROPERTY
            COMPILE_OPTIONS "${_ckre_nvtt_memcall_warning}")
endif ()

if (TARGET bimg_encode AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # The selector is constrained to [0, 15] by etcpak, but GCC cannot prove it.
    set_property(SOURCE
            "${BIMG_DIR}/3rdparty/etcpak/ProcessRGB.cpp"
            TARGET_DIRECTORY bimg_encode APPEND PROPERTY
            COMPILE_OPTIONS "-Wno-return-type")
endif ()

if (MSVC)
    # All other bgfx targets use the project default exception model.  Adding
    # it per target avoids both bx's D9025 overrides and bimg's C4530.
    foreach (_ckre_exception_target bimg bimg_decode bimg_encode bgfx)
        if (TARGET ${_ckre_exception_target})
            target_compile_options(${_ckre_exception_target} PRIVATE /EHsc)
        endif ()
    endforeach ()

    # The vendored bgfx sources are UTF-8.  Declare only the source encoding so
    # local non-English Windows code pages do not emit C4819 or alter parsing;
    # leave the execution character set unchanged for legacy runtime strings.
    foreach (_ckre_utf8_target bx bimg bimg_decode bgfx)
        if (TARGET ${_ckre_utf8_target})
            target_compile_options(${_ckre_utf8_target} PRIVATE
                    /source-charset:utf-8)
        endif ()
    endforeach ()
    if (TARGET bimg_encode)
        # nvtt combines historical Windows-1252 and UTF-8 sources.  Do not
        # force a single source charset; limit encoding diagnostics to this
        # vendored encoder target instead.
        target_compile_options(bimg_encode PRIVATE /wd4819 /wd4828)
    endif ()

    # Keep third-party conversion diagnostics scoped to the affected sources.
    set_property(SOURCE
            "${BIMG_DIR}/src/image.cpp"
            TARGET_DIRECTORY bimg APPEND PROPERTY
            COMPILE_OPTIONS "/wd4244")
    set_property(SOURCE
            "${BGFX_DIR}/src/video_vk.cpp"
            TARGET_DIRECTORY bgfx APPEND PROPERTY
            COMPILE_OPTIONS "/wd4244")

endif ()

if (TARGET bgfx AND CKRE_BGFX_OPENGLES_VERSION)
    target_compile_definitions(bgfx PRIVATE
            BGFX_CONFIG_RENDERER_OPENGLES=${CKRE_BGFX_OPENGLES_VERSION}
    )
endif ()

if (TARGET bgfx AND CKRE_BUILD_SHARED AND BGFX_LIBRARY_TYPE STREQUAL "STATIC")
    target_compile_definitions(bgfx PRIVATE BGFX_SHARED_LIB_BUILD=1)
endif ()

if (TARGET shaderc AND MINGW)
    target_compile_definitions(shaderc PRIVATE SHADERC_CONFIG_HAS_DXC=0)
endif ()
