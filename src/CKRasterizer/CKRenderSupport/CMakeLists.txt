# Diagnostics and render configuration shared without depending on rendering layers.
option(CKRE_ENABLE_PROFILING "Compile external NVTX timeline annotations (not for FPS acceptance)" OFF)
add_library(CKRenderProfiling INTERFACE)
target_include_directories(CKRenderProfiling INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(CKRenderProfiling INTERFACE
        $<BUILD_INTERFACE:CKRE_ENABLE_PROFILING=$<BOOL:${CKRE_ENABLE_PROFILING}>>)
if (CKRE_ENABLE_PROFILING)
    find_path(CKRE_NVTX_INCLUDE_DIR nvtx3/nvToolsExt.h)
    if (NOT CKRE_NVTX_INCLUDE_DIR)
        message(FATAL_ERROR "CKRE_ENABLE_PROFILING requires NVTX 3 headers; set CKRE_NVTX_INCLUDE_DIR explicitly")
    endif ()
    target_include_directories(CKRenderProfiling SYSTEM INTERFACE
            $<BUILD_INTERFACE:${CKRE_NVTX_INCLUDE_DIR}>)
    if (CMAKE_DL_LIBS)
        target_link_libraries(CKRenderProfiling INTERFACE ${CMAKE_DL_LIBS})
    endif ()
endif ()

set(CKRE_RENDER_SUPPORT_SOURCES
        CKRenderProfile.h
        CKDrawAnnotation.h
        CKDrawAnnotationParser.cpp
        CKRenderConfig.h
        CKRenderSettings.h
        CKRenderSettings.cpp
        CKDebugLogger.h
        CKRenderPerfClock.h
        CKRenderFrameCostStats.h
)
if (CKRE_ENABLE_DEBUG_LOGGER)
    list(APPEND CKRE_RENDER_SUPPORT_SOURCES CKDebugLogger.cpp)
endif ()
if (CKRE_ENABLE_RENDER_STATS)
    list(APPEND CKRE_RENDER_SUPPORT_SOURCES CKRenderPerfClock.cpp)
endif ()
if (CKRE_ENABLE_FRAME_COST_STATS)
    list(APPEND CKRE_RENDER_SUPPORT_SOURCES CKRenderFrameCostStats.cpp)
endif ()
add_library(CKRenderSupport STATIC ${CKRE_RENDER_SUPPORT_SOURCES})
target_compile_features(CKRenderSupport PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKRenderSupport PRIVATE)
target_include_directories(CKRenderSupport PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(CKRenderSupport PUBLIC CKRenderProfiling ${_ckre_rasterizer_ck2_dep} ${_ckre_rasterizer_vxmath_dep})
if (CMAKE_DL_LIBS)
    target_link_libraries(CKRenderSupport PRIVATE ${CMAKE_DL_LIBS})
endif ()
set_target_properties(CKRenderSupport PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer/Support"
        POSITION_INDEPENDENT_CODE ON
)
if (CKRE_INSTALL)
    install(TARGETS CKRenderSupport CKRenderProfiling EXPORT CKRenderEngineTargets ARCHIVE DESTINATION lib)
endif ()
