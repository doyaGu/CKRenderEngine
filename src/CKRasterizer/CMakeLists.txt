# CKRasterizer - Contains all rasterizer implementations
# This directory includes the base rasterizer library and platform-specific implementations

# Add the base rasterizer library
add_subdirectory(CKRasterizerLib)

# Add DirectX 9 rasterizer implementation
add_subdirectory(CKDX9Rasterizer)

# Additional rasterizer implementations can be added here as subdirectories
# For example:
# add_subdirectory(CKGLRasterizer)
# add_subdirectory(CKVkRasterizer)
