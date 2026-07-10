# CKRasterizerLib - v2 rasterizer base and null fallback
set(CKRASTERIZER_LIB_SOURCES
        CKRasterizer.cpp
        CKRasterizerDriver.cpp
        CKRasterizerContext.cpp
)

set(CKRASTERIZER_LIB_HEADERS
        CKRasterizerValidation.h
        ${CKRE_INCLUDE_DIR}/CKRasterizer.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerEnums.h
        ${CKRE_INCLUDE_DIR}/CKRasterizerTypes.h
)

add_library(CKRasterizerLib STATIC ${CKRASTERIZER_LIB_SOURCES} ${CKRASTERIZER_LIB_HEADERS})

target_include_directories(CKRasterizerLib
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CKRE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(_ckre_rasterizer_ck2_dep CK2)
if (CKRE_BUILD_STATIC AND TARGET CK2Static)
    set(_ckre_rasterizer_ck2_dep CK2Static)
endif ()

set(_ckre_rasterizer_vxmath_dep VxMath)
if (CKRE_BUILD_STATIC AND TARGET VxMathStatic)
    set(_ckre_rasterizer_vxmath_dep VxMathStatic)
endif ()

target_link_libraries(CKRasterizerLib PUBLIC
        ${_ckre_rasterizer_ck2_dep}
        ${_ckre_rasterizer_vxmath_dep}
)

set_target_properties(CKRasterizerLib PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        FOLDER "Rasterizer"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CKRE_BUILD_SHARED)
    set_target_properties(CKRasterizerLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

if (CKRE_INSTALL)
    install(TARGETS CKRasterizerLib
            EXPORT CKRenderEngineTargets
            ARCHIVE DESTINATION lib
    )
endif ()
