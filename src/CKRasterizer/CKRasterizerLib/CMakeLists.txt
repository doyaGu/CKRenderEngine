# Public CKRasterizer types, shared render support, and the direct NULL
# implementation. This library has no fixed-function translation or modern
# graphics backend dependency.
option(CKRE_ENABLE_PROFILING "Compile external NVTX timeline annotations (not for FPS acceptance)" OFF)

set(CKRASTERIZER_LIB_SOURCES
    CKRasterizerCapsBaseline.cpp
    CKNullRasterizer.cpp
    CKDrawAnnotationParser.cpp
    CKRenderSettings.cpp
)
if (CKRE_ENABLE_DEBUG_LOGGER)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKDebugLogger.cpp)
endif ()
if (CKRE_ENABLE_RENDER_STATS)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKRenderPerfClock.cpp)
endif ()
if (CKRE_ENABLE_FRAME_COST_STATS)
    list(APPEND CKRASTERIZER_LIB_SOURCES CKRenderFrameCostStats.cpp)
endif ()

set(CKRASTERIZER_LIB_HEADERS
    CKRasterizerCapsBaseline.h
    CKRasterizerCapsBaseline.generated.h
    CKNullRasterizer.h
    CKDebugLogger.h
    CKDrawAnnotation.h
    CKRenderConfig.h
    CKRenderFrameCostStats.h
    CKRenderPerfClock.h
    CKRenderProfile.h
    CKRenderSettings.h
    ${CKRE_INCLUDE_DIR}/CKRasterizer.h
    ${CKRE_INCLUDE_DIR}/CKRasterizerEnums.h
    ${CKRE_INCLUDE_DIR}/CKRasterizerRegistration.h
    ${CKRE_INCLUDE_DIR}/CKRasterizerResourceTypes.h
    ${CKRE_INCLUDE_DIR}/CKRasterizerTypes.h
)

add_library(CKRasterizerLib STATIC ${CKRASTERIZER_LIB_SOURCES} ${CKRASTERIZER_LIB_HEADERS})
target_compile_features(CKRasterizerLib PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKRasterizerLib PRIVATE)
target_compile_definitions(CKRasterizerLib PUBLIC
    $<BUILD_INTERFACE:CKRE_ENABLE_PROFILING=$<BOOL:${CKRE_ENABLE_PROFILING}>>
)
target_include_directories(CKRasterizerLib
    PUBLIC
    $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (CKRE_ENABLE_PROFILING)
    find_path(CKRE_NVTX_INCLUDE_DIR nvtx3/nvToolsExt.h)
    if (NOT CKRE_NVTX_INCLUDE_DIR)
        message(FATAL_ERROR "CKRE_ENABLE_PROFILING requires NVTX 3 headers; set CKRE_NVTX_INCLUDE_DIR explicitly")
    endif ()
    target_include_directories(CKRasterizerLib SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CKRE_NVTX_INCLUDE_DIR}>
    )
endif ()

target_link_libraries(CKRasterizerLib PUBLIC
    ${_ckre_rasterizer_ck2_dep}
    ${_ckre_rasterizer_vxmath_dep}
)
if (CMAKE_DL_LIBS)
    target_link_libraries(CKRasterizerLib PRIVATE ${CMAKE_DL_LIBS})
endif ()
set_target_properties(CKRasterizerLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
)

if (CKRE_INSTALL)
    install(TARGETS CKRasterizerLib
        EXPORT CKRenderEngineTargets
        ARCHIVE DESTINATION lib
    )
endif ()
