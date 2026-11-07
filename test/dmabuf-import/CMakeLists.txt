project(indium-test-dmabuf-import)

add_executable(indium-test-dmabuf-import dmabuf-import.cpp)
target_link_libraries(indium-test-dmabuf-import PRIVATE indium_kit indium_private)
set_target_properties(indium-test-dmabuf-import PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
