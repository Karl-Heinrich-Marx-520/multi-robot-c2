add_executable(c2_json_test json_test.cpp)
target_link_libraries(c2_json_test PRIVATE c2_io)
add_test(NAME json_contract COMMAND c2_json_test)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME udp_guide_integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/udp_guide_test.py" "$<TARGET_FILE:c2_center>")
    set_tests_properties(udp_guide_integration PROPERTIES TIMEOUT 20)
endif()
