add_executable(bgfx_cubes main.cpp)

set_target_properties(bgfx_cubes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "Examples"
)

target_include_directories(bgfx_cubes PRIVATE
    ${CKRE_INCLUDE_DIR}
)

target_link_libraries(bgfx_cubes PRIVATE
    CKBgfxRasterizerStatic
    CKRasterizerLib
    CK2
    VxMath
    bgfx
    bimg
    bx
)

