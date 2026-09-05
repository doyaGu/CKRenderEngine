# Diagnostics and render configuration shared without depending on rendering layers.
set(CKRE_RENDER_SUPPORT_SOURCES
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
target_link_libraries(CKRenderSupport PUBLIC ${_ckre_rasterizer_ck2_dep} ${_ckre_rasterizer_vxmath_dep})
if (CMAKE_DL_LIBS)
    target_link_libraries(CKRenderSupport PRIVATE ${CMAKE_DL_LIBS})
endif ()
set_target_properties(CKRenderSupport PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer/Support"
        POSITION_INDEPENDENT_CODE ON
)
if (CKRE_INSTALL)
    install(TARGETS CKRenderSupport EXPORT CKRenderEngineTargets ARCHIVE DESTINATION lib)
endif ()
