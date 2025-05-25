// Copyright (c) 2025 Viktor Vorobjov
/*
 * Copyright (c) 2025 Viktor Vorobjov
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

// freertos headers
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

// MicroPython headers
#include "mpconfigport.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/mpthread.h"

// ESP-IDF headers
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task.h"
#include "esp_check.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "esp_partition.h"

// Radio Spinel headers
#include "esp_radio_spinel.h"

// Zigbee headers
#include "esp_zigbee_core.h"
#include "esp_zigbee_type.h"


// Project headers
#include "main.h"
#include "mod_zig_msg.h"        // Message type constants - to micropython
#include "mod_zig_core.h"       // main config and init
#include "mod_zig_network.h"    // network functions
#include "mod_zig_devices.h"    // device functions
#include "mod_zig_handlers.h"   // event handlers
#include "mod_zig_cmd.h"        // device commands
#include "device_storage.h"     // device storage
#include "mod_zig_custom.h"     // custom cluster functions - tuya, zigbee-thermostat, etc.

//generate from esp-zigbee
#include "generate/zcl_profile.h"
#include "generate/zcl_device.h"
#include "generate/zcl_cluster.h"
#include "generate/zcl_status.h"
#include "generate/zcl_attr_type.h"
#include "generate/zcl_attr_access.h"
#include "generate/zcl_action_callback.h"
//#include "mod_zig_enum_parser.h" // Zigbee enum parser


esp32_zig_config_t zig_config = {0};  // Initialize with zeros

esp32_zig_obj_t esp32_zig_obj = {
    {&machine_zig_type},
    .config = &zig_config,
};

// main init function
mp_obj_t esp32_zig_init_helper(esp32_zig_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_name, ARG_bitrate, ARG_rcp_reset_pin, ARG_rcp_boot_pin, ARG_uart_port, ARG_uart_rx_pin, ARG_uart_tx_pin, ARG_start, ARG_storage };

    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_name,             MP_ARG_KW_ONLY | MP_ARG_OBJ,    {.u_obj =   mp_const_none   } },
        { MP_QSTR_bitrate,          MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   460800          } },   // Default bitrate for ESP32 RCP
        { MP_QSTR_rcp_reset_pin,    MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   7               } },   // Default RCP reset pin
        { MP_QSTR_rcp_boot_pin,     MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   8               } },   // Default RCP boot pin
        { MP_QSTR_uart_port,        MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   1               } },   // Default UART port
        { MP_QSTR_uart_rx_pin,      MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   4               } },   // Default RX pin
        { MP_QSTR_uart_tx_pin,      MP_ARG_KW_ONLY | MP_ARG_INT,    {.u_int =   5               } },   // Default TX pin
        { MP_QSTR_start,            MP_ARG_KW_ONLY | MP_ARG_BOOL,   {.u_bool =  true            } },   // Default start flag
        { MP_QSTR_storage,          MP_ARG_KW_ONLY | MP_ARG_OBJ,    {.u_obj =   mp_const_none   } }    // Storage callback
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Zero entire config to default values
    memset(self->config, 0, sizeof(*self->config));
    // Set default general name
    strncpy(self->config->general, "ZigBeeScrivo", sizeof(self->config->general) - 1);
    self->config->general[sizeof(self->config->general) - 1]    = '\0';

    // Process name if provided
    if (args[ARG_name].u_obj != mp_const_none) {
        const char *name = mp_obj_str_get_str(args[ARG_name].u_obj);
        strncpy(self->config->general, name, sizeof(self->config->general) - 1);
        self->config->general[sizeof(self->config->general) - 1] = '\0'; // Ensure null termination
    }

    // Set storage callback
    self->storage_cb = args[ARG_storage].u_obj;

    // load all devices from separate files
    esp_err_t dev_loaded = device_storage_load_all(self);
    if (dev_loaded != ESP_OK) {
        mp_printf(&mp_plat_print, "ZFailed to loading devices: '%s')", esp_err_to_name(dev_loaded));
        // not critical error, continue with empty device list
    }

    // Set uart parameters
    self->config->bitrate       =   args[ARG_bitrate].u_int;
    self->config->rcp_reset_pin =   args[ARG_rcp_reset_pin].u_int;
    self->config->rcp_boot_pin  =   args[ARG_rcp_boot_pin].u_int;
    self->config->uart_port     =   args[ARG_uart_port].u_int;
    self->config->uart_rx_pin   =   args[ARG_uart_rx_pin].u_int;
    self->config->uart_tx_pin   =   args[ARG_uart_tx_pin].u_int;
    
    // Check if start flag
    bool start_flag = args[ARG_start].u_bool;

    
    
    if (!start_flag) {
        ESP_LOGI(ZIGBEE_TAG, "ZIGBEE: Gateway initialized, commissioning start deferred ");
    } else {
        // Start Zigbee gateway commissioning now
        ESP_LOGI(ZIGBEE_TAG, "ZIGBEE: Starting deferred Zigbee gateway commissioning ");
        esp_err_t err = esp32_zig_start_gateway(self);                                                  // Point to start Zigbee gateway
        if (err != ESP_OK) {
            mp_raise_msg_varg(&mp_type_RuntimeError, "Failed to start Zigbee Gateway: %s", esp_err_to_name(err));
            return mp_const_none;
        }
    }

    self->config->network_formed = true;
    return mp_const_none;
}



// ZIG(bus, ...) No argument to get the object
// If no arguments are provided, the initialized object will be returned
static mp_obj_t esp32_zig_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // Simplified argument check
    mp_arg_check_num(n_args, n_kw, 0, MP_OBJ_FUN_ARGS_MAX, true);

    esp32_zig_obj_t *self = &esp32_zig_obj;

    // Create message queue immediately to not miss Zigbee events
    if (self->message_queue == NULL) {
        self->message_queue = xQueueCreate(32, sizeof(zigbee_message_t));
        if (self->message_queue == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, "Failed to create message queue");
        }
    }

    // If there are arguments, pass them to init_helper
    if (n_args > 0 || n_kw > 0) {
        if (self->config->network_formed) {
            // The caller is requesting a reconfiguration of the hardware
            // this can only be done if the hardware is in init mode
            //zig_deinit(self);
            // mp_raise_msg(&mp_type_RuntimeError, "Device is already initialized");
            // return mp_const_none;

            mp_printf(&mp_plat_print, "ZIG device already initialized (name='%s')\n", 
                     self->config->general);
                     
            return MP_OBJ_FROM_PTR(self);

        }

        
        self->tx_callback = mp_const_none;
        self->rx_callback = mp_const_none;
        self->storage_cb = mp_const_none;
        self->irq_handler = NULL;
        self->gateway_task = NULL;

        // start the peripheral
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        esp32_zig_init_helper(self, n_args, args, &kw_args);
        
    }
    
    return MP_OBJ_FROM_PTR(self);
}

// init(name="ZIG device", bitrate=460800, gateway=False)
static mp_obj_t esp32_zig_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is already initialized");
        return mp_const_none;
    }
    return esp32_zig_init_helper(self, n_args - 1, pos_args + 1, kw_args);
}

static MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_init_obj, 1, esp32_zig_init);

static void esp32_zig_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->config->network_formed) {
   
        // Update network information before displaying by calling the existing function
        if (esp_zb_is_started()) {
            // Call our function to update the network status
            esp32_zig_update_network_status(self_in);
        }
        
        mp_printf(print, "ZIGBEE GATEWAY(name=%s, active=%s, PAN ID=0x%04x, channel=%u)",
            self->config->general,
            self->config->network_formed ? "yes" : "no",
            self->config->pan_id,
            self->config->channel);

    } else {
        mp_printf(print, "Device is not initialized");
    }
}


// ZIG.get_info()
static mp_obj_t esp32_zig_get_info(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Create a dictionary for returning information about ZIG
    mp_obj_t info_dict = mp_obj_new_dict(8);
    
    // Fill the dictionary with state data
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_name), 
                     mp_obj_new_str(self->config->general, strlen(self->config->general)));
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_bitrate), 
                     mp_obj_new_int(self->config->bitrate));

    // Update network information first by calling the existing function
    if (esp_zb_is_started()) {
        // Use our improved function to update the network status
        esp32_zig_update_network_status(self_in);
    }
    
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_gateway), 
                        mp_obj_new_bool(true));
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_network_formed), 
                        mp_obj_new_bool(self->config->network_formed));
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_pan_id), 
                        mp_obj_new_int(self->config->pan_id));
    mp_obj_dict_store(info_dict, MP_OBJ_NEW_QSTR(MP_QSTR_channel), 
                        mp_obj_new_int(self->config->channel));
    
                     
    return info_dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_get_info_obj, esp32_zig_get_info);



// ZIG local dictionary
// This dictionary is used to store the attributes of the ZIG object
static const mp_rom_map_elem_t esp32_zig_locals_dict_table[] = {
    // ZIG ATTRIBUTES
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ZIG) },
    // Micropython Generic API
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&esp32_zig_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_info), MP_ROM_PTR(&esp32_zig_get_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_to_factory), MP_ROM_PTR(&esp32_zig_reset_to_factory_obj) },

    // Network Management API
    { MP_ROM_QSTR(MP_QSTR_open_network), MP_ROM_PTR(&esp32_zig_open_network_obj) },
    { MP_ROM_QSTR(MP_QSTR_close_network), MP_ROM_PTR(&esp32_zig_close_network_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_network_info), MP_ROM_PTR(&esp32_zig_get_network_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_update_network_status), MP_ROM_PTR(&esp32_zig_update_network_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan_networks), MP_ROM_PTR(&esp32_zig_scan_networks_obj) },
    { MP_ROM_QSTR(MP_QSTR_start_network), MP_ROM_PTR(&esp32_zig_start_network_obj) },
    
    // Device Management API
    //{ MP_ROM_QSTR(MP_QSTR_get_device_list),             MP_ROM_PTR(&esp32_zig_get_device_list_obj)          }, // only shord id list
    //{ MP_ROM_QSTR(MP_QSTR_get_device),                  MP_ROM_PTR(&esp32_zig_get_device_obj)               }, // get device by short id



    // Micropython CMD API
    
    { MP_ROM_QSTR(MP_QSTR_send_command), MP_ROM_PTR(&esp32_zig_send_command_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_recv_callback), MP_ROM_PTR(&esp32_zig_set_recv_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&esp32_zig_recv_obj) },
    //use for asyncio
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&esp32_zig_any_obj) },

    { MP_ROM_QSTR(MP_QSTR_bind_cluster), MP_ROM_PTR(&esp32_zig_bind_cluster_obj) },
    { MP_ROM_QSTR(MP_QSTR_configure_report), MP_ROM_PTR(&esp32_zig_configure_report_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_report_config), MP_ROM_PTR(&esp32_zig_set_report_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_attr), MP_ROM_PTR(&esp32_zig_read_attr_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_attr), MP_ROM_PTR(&esp32_zig_write_attr_obj) },

    // Message type constants
    { MP_ROM_QSTR(MP_QSTR_MSG), MP_ROM_PTR(&zig_msg_module) },

    
};

static MP_DEFINE_CONST_DICT(esp32_zig_locals_dict, esp32_zig_locals_dict_table);

// Python object definition
MP_DEFINE_CONST_OBJ_TYPE(
    machine_zig_type,
    MP_QSTR_ZIG,
    MP_TYPE_FLAG_NONE,
    make_new, esp32_zig_make_new,
    print, esp32_zig_print,
    locals_dict, (mp_obj_dict_t *)&esp32_zig_locals_dict
);

MP_REGISTER_MODULE(MP_QSTR_ZIG, machine_zig_type);
