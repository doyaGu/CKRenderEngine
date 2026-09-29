# Dependency direction: concrete rasterizers -> CKFFPLib -> CKRasterizerLib and
# the CK-independent CKShaderJit.
set(_ckre_rasterizer_ck2_dep CK2)
if (CKRE_BUILD_STATIC AND TARGET CK2Static)
    set(_ckre_rasterizer_ck2_dep CK2Static)
endif ()
set(_ckre_rasterizer_vxmath_dep VxMath)
if (CKRE_BUILD_STATIC AND TARGET VxMathStatic)
    set(_ckre_rasterizer_vxmath_dep VxMathStatic)
endif ()

add_subdirectory(CKRasterizerLib)
add_subdirectory(CKShaderJit)
add_subdirectory(CKFFPLib)
include(CKRERasterizers)
ckre_get_rasterizers(_ckre_enabled_rasterizers _ckre_disabled_rasterizers FALSE)
foreach (_ckre_rasterizer IN LISTS _ckre_enabled_rasterizers)
    add_subdirectory(${_ckre_rasterizer})
endforeach ()
