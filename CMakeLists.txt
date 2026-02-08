cmake_minimum_required(VERSION 3.16)

# Prevent in-tree builds
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please create a separate build directory.")
endif ()

project(CKRenderEngine VERSION 0.1.0 LANGUAGES CXX)

# =============================================================================
# Determine if this is a top-level project
# =============================================================================
if (CMAKE_VERSION VERSION_LESS 3.21)
    if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(CKRE_IS_TOP_LEVEL ON)
    else ()
        set(CKRE_IS_TOP_LEVEL OFF)
    endif ()
else ()
    set(CKRE_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})
endif ()

# =============================================================================
# Project options
# =============================================================================
option(CKRE_BUILD_SHARED "Build shared library" ON)
option(CKRE_BUILD_STATIC "Build static library" OFF)
option(CKRE_BUILD_TESTS "Build tests" OFF)
option(CKRE_INSTALL "Generate install target" ${CKRE_IS_TOP_LEVEL})

# =============================================================================
# Common paths
# =============================================================================
# Keep include directory in one place; subdirectories should reference this.
set(CKRE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# =============================================================================
# Platform check
# =============================================================================
if (NOT WIN32)
    message(FATAL_ERROR "[CKRenderEngine] Only Windows platform is supported")
endif ()

# Enable RC language for resource files
enable_language(RC)

# =============================================================================
# C++ standard
# =============================================================================
# When built as part of Ballanced, inherit the top-level policy.
# When built standalone, default to C++17.
if (CKRE_IS_TOP_LEVEL)
    if (NOT CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 17)
    endif ()
    set(CMAKE_CXX_STANDARD_REQUIRED YES)
endif ()

# =============================================================================
# Top-level project settings
# =============================================================================
if (CKRE_IS_TOP_LEVEL)
    # Set build type
    get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
        message(STATUS "[CKRenderEngine] Setting build type to 'Release'")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif ()

    # Set install prefix
    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
        message(STATUS "[CKRenderEngine] Setting install directory to ${CMAKE_INSTALL_PREFIX}")
    endif ()

    # Enable compile commands export
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

# =============================================================================
# Compiler settings
# =============================================================================
if (MSVC)
    add_compile_definitions(
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
    )
    # Disable specific warnings if needed
    add_compile_options(/wd4251)  # Disable "needs to have dll-interface" warning
endif ()

# =============================================================================
# Virtools SDK
# =============================================================================
if (NOT TARGET VxMath OR NOT TARGET CK2)
    set(VIRTOOLS_SDK_PATH "" CACHE PATH "Path to the Virtools SDK")
    option(VIRTOOLS_SDK_FETCH_FROM_GIT "Fetch Virtools SDK from git if not found" OFF)
    set(VIRTOOLS_SDK_FETCH_FROM_GIT_PATH "" CACHE FILEPATH "Location to download SDK")

    if (NOT VIRTOOLS_SDK_PATH)
        if (DEFINED ENV{VIRTOOLS_SDK_PATH})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK_PATH}" CACHE PATH "Path to the Virtools SDK" FORCE)
        elseif (DEFINED ENV{VIRTOOLS_SDK})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK}" CACHE PATH "Path to the Virtools SDK" FORCE)
        endif ()
    endif ()

    if (NOT VIRTOOLS_SDK_PATH)
        if (VIRTOOLS_SDK_FETCH_FROM_GIT)
            include(FetchContent)
            if (VIRTOOLS_SDK_FETCH_FROM_GIT_PATH)
                set(FETCHCONTENT_BASE_DIR ${VIRTOOLS_SDK_FETCH_FROM_GIT_PATH})
            endif ()
            message(STATUS "[CKRenderEngine] Fetching Virtools SDK from git...")
            FetchContent_Declare(
                    virtools_sdk
                    GIT_REPOSITORY https://github.com/doyaGu/Virtools-SDK-2.1.git
                    GIT_TAG main
            )
            FetchContent_MakeAvailable(virtools_sdk)
            set(VIRTOOLS_SDK_PATH "${virtools_sdk_SOURCE_DIR}" CACHE PATH "Path to the Virtools SDK" FORCE)
        else ()
            message(FATAL_ERROR "[CKRenderEngine] VIRTOOLS_SDK_PATH not set. Please specify the path or enable VIRTOOLS_SDK_FETCH_FROM_GIT")
        endif ()
    endif ()

    find_path(VIRTOOLS_SDK_INCLUDE_DIR CKAll.h
            HINTS $ENV{VIRTOOLS_SDK_PATH} ${VIRTOOLS_SDK_PATH}
            PATH_SUFFIXES Include Includes include includes
    )
    if (NOT VIRTOOLS_SDK_INCLUDE_DIR)
        message(FATAL_ERROR "[CKRenderEngine] Virtools SDK headers not found. Expected CKAll.h under ${VIRTOOLS_SDK_PATH}.")
    endif ()

    foreach (_lib IN ITEMS CK2 VxMath)
        find_library(VIRTOOLS_SDK_${_lib}_LIB NAMES ${_lib}
                HINTS $ENV{VIRTOOLS_SDK_PATH} ${VIRTOOLS_SDK_PATH}
                PATH_SUFFIXES Lib lib
        )
        if (NOT VIRTOOLS_SDK_${_lib}_LIB)
            message(FATAL_ERROR "[CKRenderEngine] Virtools SDK library '${_lib}' not found under ${VIRTOOLS_SDK_PATH}.")
        endif ()

        if (NOT TARGET ${_lib})
            add_library(${_lib} UNKNOWN IMPORTED)
            set_target_properties(${_lib} PROPERTIES
                    IMPORTED_LOCATION "${VIRTOOLS_SDK_${_lib}_LIB}"
                    INTERFACE_INCLUDE_DIRECTORIES "${VIRTOOLS_SDK_INCLUDE_DIR}"
            )
            target_compile_options(${_lib} INTERFACE
                    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:MSVC>:/Zc:strictStrings->>
                    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU>:-fpermissive>>
                    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU>:-Wno-write-strings>>
            )
        endif ()
    endforeach ()
endif ()

# =============================================================================
# Dependencies
# =============================================================================
# Find or fetch VxMath
if (NOT TARGET VxMath)
    find_package(VxMath QUIET)
    if (NOT VxMath_FOUND)
        message(STATUS "[CKRenderEngine] VxMath not found, fetching from parent directory...")
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../VxMath/CMakeLists.txt")
            if (CKRE_BUILD_STATIC)
                set(VXMATH_BUILD_STATIC ON CACHE BOOL "" FORCE)
            endif ()
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../VxMath" "${CMAKE_BINARY_DIR}/VxMath")
        else ()
            message(FATAL_ERROR "[CKRenderEngine] VxMath not found. Please install VxMath or place it in the parent directory")
        endif ()
    endif ()
endif ()

# Find or fetch CK2
if (NOT TARGET CK2)
    find_package(CK2 QUIET)
    if (NOT CK2_FOUND)
        message(STATUS "[CKRenderEngine] CK2 not found, fetching from parent directory...")
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CK2/CMakeLists.txt")
            if (CKRE_BUILD_STATIC)
                set(CK2_BUILD_STATIC ON CACHE BOOL "" FORCE)
            endif ()
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CK2" "${CMAKE_BINARY_DIR}/CK2")
        else ()
            message(FATAL_ERROR "[CKRenderEngine] CK2 not found. Please install CK2 or place it in the parent directory")
        endif ()
    endif ()
endif ()

# =============================================================================
# Build library
# =============================================================================
add_subdirectory(src)

# =============================================================================
# Tests
# =============================================================================
if (CKRE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# =============================================================================
# Installation
# =============================================================================
if (CKRE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install public headers to include root
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
            DESTINATION include
            FILES_MATCHING PATTERN "*.h"
    )

    # Render engines are runtime components, no CMake config needed
endif ()

# =============================================================================
# Configuration summary
# =============================================================================
if (CKRE_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "============================================================")
    message(STATUS "CKRenderEngine Configuration Summary")
    message(STATUS "============================================================")
    message(STATUS "  Version:              ${PROJECT_VERSION}")
    message(STATUS "  Build Type:           ${CMAKE_BUILD_TYPE}")
    message(STATUS "  Build Shared:         ${CKRE_BUILD_SHARED}")
    message(STATUS "  Build Static:         ${CKRE_BUILD_STATIC}")
    message(STATUS "  Build Tests:          ${CKRE_BUILD_TESTS}")
    if (VIRTOOLS_SDK_PATH)
        message(STATUS "  Virtools SDK:         ${VIRTOOLS_SDK_PATH}")
    endif ()
    message(STATUS "  Install:              ${CKRE_INSTALL}")
    message(STATUS "  Install Prefix:       ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "============================================================")
    message(STATUS "")
endif ()