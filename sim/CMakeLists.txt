foreach(device IN ITEMS car uav dog)
    add_executable(sim_${device} sim_${device}/main.cpp)
    target_link_libraries(sim_${device} PRIVATE c2_project_options c2_protocol)
endforeach()

target_link_libraries(sim_car PRIVATE c2_io)
