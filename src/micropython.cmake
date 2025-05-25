# Create INTERFACE library for our C module
add_library(usermod_esp32zig INTERFACE)

# Add source files for the module
target_sources(usermod_esp32zig INTERFACE

    # Generated ZCL type modules
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_profile.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_device.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_cluster.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_status.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_attr_type.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_attr_access.c
    ${CMAKE_CURRENT_LIST_DIR}/generate/zcl_action_callback.c
  
    # modules
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_msg.c
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_handlers.c
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_network.c
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_core.c
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_cmd.c
    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_devices.c
    
    # device management - new implementation
    ${CMAKE_CURRENT_LIST_DIR}/device_manager.c
    ${CMAKE_CURRENT_LIST_DIR}/device_storage.c
    ${CMAKE_CURRENT_LIST_DIR}/device_json.c

    ${CMAKE_CURRENT_LIST_DIR}/mod_zig_custom.c
    
    # main
    ${CMAKE_CURRENT_LIST_DIR}/main.c
   
)

# Add paths to header files for the module
target_include_directories(usermod_esp32zig INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${IDF_PATH}/components/json/cJSON
)

# Add paths to header files for the module
target_compile_definitions(usermod_esp32zig INTERFACE )

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_esp32zig)

