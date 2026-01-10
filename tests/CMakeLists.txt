add_executable(nvstripifier_tests
    test_nvstripifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/NvStripifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/MeshAdjacency.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RadixSort.cpp
)

target_include_directories(nvstripifier_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(nvstripifier_tests PRIVATE
    CK2
    VxMath
)

set_target_properties(nvstripifier_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test(NAME nvstripifier_tests COMMAND nvstripifier_tests)

add_executable(meshstriper_tests
    test_meshstriper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/MeshStriper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/MeshAdjacency.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RadixSort.cpp
)

target_include_directories(meshstriper_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(meshstriper_tests PRIVATE
    CK2
    VxMath
)

set_target_properties(meshstriper_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test(NAME meshstriper_tests COMMAND meshstriper_tests)