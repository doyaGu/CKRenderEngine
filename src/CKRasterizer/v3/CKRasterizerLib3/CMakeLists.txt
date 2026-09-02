# CKRasterizerLib3 - CKRasterizer v3 contract headers, NULL rasterizer and
# capability baseline (phase 0). Coexists with the v2 CKRasterizerLib; phase 1
# moves the headers over include/ and merges this target into CKRasterizerLib.

set(CKRASTERIZER_LIB3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
# The capability baseline lives in the v2 CKRasterizerLib directory because the
# bgfx plugin (still v2 in phase 0) consumes it too. This library compiles the
# same source; no executable links both libraries before phase 1 merges them.
set(CKRASTERIZER_LIB_V2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CKRasterizerLib")

set(CKRASTERIZER_LIB3_SOURCES
        CKNullRasterizer.cpp
        CKNullRasterizerContext.cpp
        ${CKRASTERIZER_LIB_V2_DIR}/CKRasterizerCapsBaseline.cpp
)

set(CKRASTERIZER_LIB3_HEADERS
        CKNullRasterizer.h
        ${CKRASTERIZER_LIB_V2_DIR}/CKRasterizerCapsBaseline.h
        ${CKRASTERIZER_LIB_V2_DIR}/CKRasterizerCapsBaseline.generated.h
        ${CKRASTERIZER_LIB3_INCLUDE_DIR}/CKRasterizer.h
        ${CKRASTERIZER_LIB3_INCLUDE_DIR}/CKRasterizerEnums.h
        ${CKRASTERIZER_LIB3_INCLUDE_DIR}/CKRasterizerTypes.h
)

add_library(CKRasterizerLib3 STATIC ${CKRASTERIZER_LIB3_SOURCES} ${CKRASTERIZER_LIB3_HEADERS})
target_compile_features(CKRasterizerLib3 PRIVATE cxx_std_20)

# The v3 include directory must shadow the v2 include/ directory for every
# consumer of this target, so it is PUBLIC and listed first.
target_include_directories(CKRasterizerLib3
        PUBLIC
        $<BUILD_INTERFACE:${CKRASTERIZER_LIB3_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CKRASTERIZER_LIB_V2_DIR}>
)

set(_ckre_rasterizer3_ck2_dep CK2)
if (CKRE_BUILD_STATIC AND TARGET CK2Static)
    set(_ckre_rasterizer3_ck2_dep CK2Static)
endif ()

set(_ckre_rasterizer3_vxmath_dep VxMath)
if (CKRE_BUILD_STATIC AND TARGET VxMathStatic)
    set(_ckre_rasterizer3_vxmath_dep VxMathStatic)
endif ()

target_link_libraries(CKRasterizerLib3 PUBLIC
        ${_ckre_rasterizer3_ck2_dep}
        ${_ckre_rasterizer3_vxmath_dep}
)

set_target_properties(CKRasterizerLib3 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CKRE_BUILD_SHARED)
    set_target_properties(CKRasterizerLib3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()
