# Runtime shader compiler for fixed-function fragment and vertex programs. It
# emits SPIR-V, and DXBC with DirectX.
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
    README.md
)

if (CKRE_ENABLE_DIRECTX)
    list(APPEND CKSHADERJIT_SOURCES CKJitDxbc.cpp)
    list(APPEND CKSHADERJIT_HEADERS CKJitDxbc.h)
endif ()

function(ckre_add_shader_jit_library TARGET_NAME VXMATH_DEP)
    add_library(${TARGET_NAME} STATIC ${CKSHADERJIT_SOURCES} ${CKSHADERJIT_HEADERS})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    ckre_apply_diagnostic_definitions(${TARGET_NAME} PRIVATE)
    target_include_directories(${TARGET_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
    target_link_libraries(${TARGET_NAME}
        PUBLIC ${VXMATH_DEP}
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endfunction()

ckre_add_shader_jit_library(CKShaderJit ${_ckre_rasterizer_vxmath_dep})
if (CKRE_BUILD_SHARED AND CKRE_BUILD_STATIC)
    ckre_add_shader_jit_library(CKShaderJitShared VxMath)
endif ()
