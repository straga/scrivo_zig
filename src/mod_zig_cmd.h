// Copyright (c) 2025 Viktor Vorobjov
// Header file for device management
#ifndef MOD_ZIG_CMD_H
#define MOD_ZIG_CMD_H

#include "mod_zig_types.h"
#include "esp_zigbee_type.h"
#include "esp_zigbee_core.h"
#include "esp_err.h"

#include "main.h"
#include "py/obj.h"
#include "py/runtime.h"



//Send Command
extern const mp_obj_fun_builtin_var_t   esp32_zig_send_command_obj;               // Send command to device

extern const mp_obj_fun_builtin_fixed_t esp32_zig_set_recv_callback_obj;          // Set callback for receiving messages
extern const mp_obj_fun_builtin_var_t   esp32_zig_recv_obj;                       // Receive messages from queue
extern const mp_obj_fun_builtin_fixed_t esp32_zig_any_obj;                        // Check if there are messages in the queue

extern const mp_obj_fun_builtin_var_t esp32_zig_bind_cluster_obj;                 // Bind cluster to device
extern const mp_obj_fun_builtin_var_t esp32_zig_configure_report_obj;             // Configure report for device
extern const mp_obj_fun_builtin_var_t esp32_zig_set_report_config_obj;           // Set report configuration for device
extern const mp_obj_fun_builtin_var_t esp32_zig_read_attr_obj;                   // Read attribute from device
extern const mp_obj_fun_builtin_var_t esp32_zig_write_attr_obj;                  // Write attribute to device



// Global variables 
// Device list is now handled by device_manager.c

// Callback prototypes
extern void bind_cb(esp_zb_zdp_status_t zdo_status, void *user_ctx);


// // Function objects for device management
// extern const mp_obj_fun_builtin_fixed_t esp32_zig_get_device_list_obj;
// extern const mp_obj_fun_builtin_var_t esp32_zig_update_device_manufacturer_obj;
// extern const mp_obj_fun_builtin_fixed_t esp32_zig_save_devices_obj;
// extern const mp_obj_fun_builtin_fixed_t esp32_zig_load_devices_obj;
// extern const mp_obj_fun_builtin_var_t esp32_zig_get_device_detail_obj;
// extern const mp_obj_fun_builtin_var_t esp32_zig_check_device_obj;
// extern const mp_obj_fun_builtin_var_t esp32_zig_remove_device_obj;

// // Helper functions for other modules to use
// void update_device_last_seen(uint16_t short_addr);
// bool is_device_available(uint16_t short_addr);
// zigbee_device_t* find_device_by_short_addr(uint16_t short_addr);

// // Function to directly save devices from other modules
// mp_obj_t esp32_zig_save_devices(mp_obj_t self_in);

// // Function to directly load devices from other modules
// mp_obj_t esp32_zig_load_devices(mp_obj_t self_in);

#endif // MOD_ZIG_CMD_H
