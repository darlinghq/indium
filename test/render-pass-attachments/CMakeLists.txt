project(indium-test-render-pass-attachments)

add_executable(indium-test-render-pass-attachments render-pass-attachments.cpp)
target_link_libraries(indium-test-render-pass-attachments PRIVATE indium_kit indium_private)
set_target_properties(indium-test-render-pass-attachments PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
