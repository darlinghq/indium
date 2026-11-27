project(indium-test-extended-range-present)

find_package(X11 REQUIRED)

add_executable(indium-test-extended-range-present extended-range-present.cpp)
target_include_directories(indium-test-extended-range-present PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(indium-test-extended-range-present PRIVATE indium_kit indium_private ${X11_LIBRARIES})
set_target_properties(indium-test-extended-range-present PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
