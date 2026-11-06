# Public CKRasterizer types, shared render support, and the direct NULL
# implementation. This library has no fixed-function translation or modern
# graphics backend dependency.
option(CKRE_ENABLE_PROFILING "Compile external NVTX timeline annotations (not for FPS acceptance)" OFF)

set(CKRASTERIZER_LIB_SOURCES
    CKRasterizer.cpp
    CKRasterizerCapsBaseline.cpp
    CKNullRasterizerContext.cpp
    CKNullRasterizerLifecycle.cpp
    CKRasterizerDriverCaps.cpp
    CKRasterizerDrawMarker.cpp
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
    CKNullRasterizerInternal.h
    CKRasterizerDriverCaps.h
    CKDebugLogger.h
    CKRasterizerDrawMarker.h
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

function(ckre_add_rasterizer_library TARGET_NAME CK2_DEP VXMATH_DEP)
    add_library(${TARGET_NAME} STATIC ${CKRASTERIZER_LIB_SOURCES} ${CKRASTERIZER_LIB_HEADERS})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    ckre_apply_diagnostic_definitions(${TARGET_NAME} PRIVATE)
    target_compile_definitions(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:CKRE_ENABLE_PROFILING=$<BOOL:${CKRE_ENABLE_PROFILING}>>
    )
    target_include_directories(${TARGET_NAME}
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
        target_include_directories(${TARGET_NAME} SYSTEM PUBLIC
            $<BUILD_INTERFACE:${CKRE_NVTX_INCLUDE_DIR}>
        )
    endif ()

    target_link_libraries(${TARGET_NAME} PUBLIC
        ${CK2_DEP}
        ${VXMATH_DEP}
    )
    if (CMAKE_DL_LIBS)
        target_link_libraries(${TARGET_NAME} PRIVATE ${CMAKE_DL_LIBS})
    endif ()
    set_target_properties(${TARGET_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
    target_link_libraries(${TARGET_NAME} PRIVATE SDL3::SDL3)

    if (CKRE_INSTALL)
        install(TARGETS ${TARGET_NAME}
            EXPORT CKRenderEngineTargets
            ARCHIVE DESTINATION lib
        )
    endif ()
endfunction()

ckre_add_rasterizer_library(CKRasterizerLib
    ${_ckre_rasterizer_ck2_dep} ${_ckre_rasterizer_vxmath_dep})
if (CKRE_BUILD_SHARED AND CKRE_BUILD_STATIC)
    ckre_add_rasterizer_library(CKRasterizerLibShared CK2 VxMath)
endif ()
