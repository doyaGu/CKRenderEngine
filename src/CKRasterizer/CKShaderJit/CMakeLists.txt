# Runtime shader compiler for fixed-function fragment programs. It emits
# SPIR-V, and DXBC with DirectX.
set(CKSHADERJIT_SOURCES
    CKJitBuilder.cpp
    CKJitIR.cpp
    CKJitSpirv.cpp
)

set(CKSHADERJIT_HEADERS
    CKJitBuilder.h
    CKJitIR.h
    CKJitOps.def
    CKJitSpirv.h
)

if (CKRE_ENABLE_DIRECTX)
    list(APPEND CKSHADERJIT_SOURCES CKJitDxbc.cpp)
    list(APPEND CKSHADERJIT_HEADERS CKJitDxbc.h)
endif ()

add_library(CKShaderJit STATIC ${CKSHADERJIT_SOURCES} ${CKSHADERJIT_HEADERS})
target_compile_features(CKShaderJit PRIVATE cxx_std_20)
ckre_apply_diagnostic_definitions(CKShaderJit PRIVATE)
target_include_directories(CKShaderJit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(CKShaderJit
    PUBLIC ${_ckre_rasterizer_vxmath_dep}
)
set_target_properties(CKShaderJit PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Rasterizer"
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
