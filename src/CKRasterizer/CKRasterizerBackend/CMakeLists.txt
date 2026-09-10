# Generic resources and program interfaces used by translated rasterizers.
# Test recorders and the direct NULL rasterizer live outside this target.
add_library(CKRasterizerBackend STATIC
        CKRasterizerBackend.h
        CKRasterizerBackendEnums.h
        CKRasterizerBackendTypes.h
        CKRasterizerValidation.h
        CKBackendDrawData.h
        CKBackendProgram.h
        CKBackendProgram.cpp
        CKBackendProgramLayout.h
        CKBackendProgramLayout.cpp
)
target_compile_features(CKRasterizerBackend PUBLIC cxx_std_17)
target_include_directories(CKRasterizerBackend PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(CKRasterizerBackend PUBLIC ${_ckre_rasterizer_ck2_dep} ${_ckre_rasterizer_vxmath_dep})
set_target_properties(CKRasterizerBackend PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer/Backend"
        POSITION_INDEPENDENT_CODE ON
)
if (CKRE_INSTALL)
    install(TARGETS CKRasterizerBackend EXPORT CKRenderEngineTargets ARCHIVE DESTINATION lib)
endif ()
