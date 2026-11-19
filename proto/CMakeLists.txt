add_library(c2_protocol INTERFACE)
target_include_directories(c2_protocol INTERFACE "${PROJECT_SOURCE_DIR}")
target_link_libraries(c2_protocol INTERFACE c2_project_options)
