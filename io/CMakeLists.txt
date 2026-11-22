add_library(c2_io INTERFACE)
target_link_libraries(c2_io INTERFACE c2_protocol)

add_library(c2_io_mock mock/mock_link.cpp)
target_link_libraries(c2_io_mock PUBLIC c2_io)
