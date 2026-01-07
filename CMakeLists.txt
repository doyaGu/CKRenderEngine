cmake_minimum_required(VERSION 3.12)

# Only check for in-tree builds when this is the top-level project
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
        message(FATAL_ERROR "In-tree builds are not supported. Run CMake from a separate directory: cmake -B build")
    endif ()
endif ()

project(CKRenderEngine VERSION 0.1.0 LANGUAGES CXX)

# Determine if CKRenderEngine is built as a subproject (using add_subdirectory) or as a standalone project
set(CKRE_IS_TOPLEVEL_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CKRE_IS_TOPLEVEL_PROJECT ON)
endif ()

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(CKRE_INSTALL "Generate install target" ${CKRE_IS_TOPLEVEL_PROJECT})

# Enable RC language for resource files only if building shared
if (BUILD_SHARED_LIBS)
    enable_language(RC)
endif ()

# Only set CXX standard if not already set by parent project
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif ()
set(CMAKE_CXX_STANDARD_REQUIRED NO)
set(CMAKE_CXX_EXTENSIONS YES)

# Add path for custom modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (NOT WIN32)
    message(FATAL_ERROR "Only support Windows.")
endif ()

# Only set these properties when this is the top-level project
if (CKRE_IS_TOPLEVEL_PROJECT)
    # Use relative paths
    if (WIN32)
        set(CMAKE_USE_RELATIVE_PATHS TRUE)
        set(CMAKE_SUPPRESS_REGENERATION TRUE)
    endif ()

    # Use folders to organize targets in an IDE
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(STATUS "Setting build type to 'Release' as no build type was specified")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type (Debug/Release)" FORCE)
    endif ()

    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install" CACHE PATH "The root directory of the installation" FORCE)
        message(STATUS "Setting default install directory to ${CMAKE_INSTALL_PREFIX} as no install directory was specified")
    endif ()
endif ()

# Disable msvc unsafe warnings
add_compile_definitions(
		$<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
		$<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)

find_package(VirtoolsSDK REQUIRED)

set(CKRE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CKRE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src)

# Installation rules
if (CKRE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install targets
    install(TARGETS CK2_3D
        EXPORT CKRenderEngineTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Install headers
    install(DIRECTORY ${CKRE_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )

    # Export targets
    install(EXPORT CKRenderEngineTargets
        FILE CKRenderEngineTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKRenderEngine
    )

    # Create package config files
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CKRenderEngineConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CKRenderEngineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKRenderEngine
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/CKRenderEngineConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install package config files
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CKRenderEngineConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CKRenderEngineConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKRenderEngine
    )
endif ()