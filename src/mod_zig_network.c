// Copyright (c) 2025 Viktor Vorobjov
#include "py/runtime.h"
#include "py/obj.h"

#include "esp_log.h"

#include "main.h"
#include "mod_zig_network.h"
#include "zdo/esp_zigbee_zdo_command.h"

// open_network(duration=180) - Open Zigbee network for new devices to join
static mp_obj_t esp32_zig_open_network(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_duration, MP_ARG_INT, {.u_int = 180} },
    };
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    // Check if device is initialized and is a gateway
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is not initialized");
        return mp_const_none;
    }
    
    
    // Get duration parameter
    uint8_t duration = (uint8_t)args[0].u_int;
    
    // Open network for specified duration
    ZB_LOCK();
    esp_err_t err = esp_zb_bdb_open_network(duration);
    ZB_UNLOCK();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, "Failed to open network: %s", esp_err_to_name(err));
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_open_network_obj, 1, esp32_zig_open_network);

// close_network() - Close Zigbee network for new devices
static mp_obj_t esp32_zig_close_network(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Check if device is initialized and is a gateway
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is not initialized");
        return mp_const_none;
    }
    
    
    // Close network
    ZB_LOCK();
    esp_err_t err = esp_zb_bdb_close_network();
    ZB_UNLOCK();
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, "Failed to close network: %s", esp_err_to_name(err));
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_close_network_obj, esp32_zig_close_network);


// get_network_info() - Get information about the Zigbee network
mp_obj_t esp32_zig_get_network_info(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Check if device is initialized and is a gateway
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is not initialized");
        return mp_const_none;
    }
    
    
    // Update network state from Zigbee stack
    uint16_t current_pan_id = esp_zb_get_pan_id();
    uint8_t current_channel = esp_zb_get_current_channel();
    esp_zb_ieee_addr_t ext_pan_id;
    esp_zb_get_extended_pan_id(ext_pan_id);
    
    // Network is considered formed if PAN ID is not 0 or 0xFFFF
    bool is_network_formed = (current_pan_id != 0 && current_pan_id != 0xFFFF);
    
    // Update data in the config object
    self->config->network_formed = is_network_formed;
    self->config->pan_id = current_pan_id;
    self->config->channel = current_channel;
    
    // Create a dictionary with network information
    mp_obj_t net_dict = mp_obj_new_dict(5);
    
    // Fill dictionary with network data
    mp_obj_dict_store(net_dict, MP_OBJ_NEW_QSTR(MP_QSTR_network_formed), 
                     mp_obj_new_bool(is_network_formed));
                     
    mp_obj_dict_store(net_dict, MP_OBJ_NEW_QSTR(MP_QSTR_pan_id), 
                     mp_obj_new_int(current_pan_id));
                     
    mp_obj_dict_store(net_dict, MP_OBJ_NEW_QSTR(MP_QSTR_channel), 
                     mp_obj_new_int(current_channel));
                     
    mp_obj_dict_store(net_dict, MP_OBJ_NEW_QSTR(MP_QSTR_short_address), 
                     mp_obj_new_int(esp_zb_get_short_address()));
    
    // Format extended PAN ID
    char ext_pan_id_str[50];
    int len = snprintf(ext_pan_id_str, sizeof(ext_pan_id_str), "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             ext_pan_id[0], ext_pan_id[1], ext_pan_id[2], ext_pan_id[3], 
             ext_pan_id[4], ext_pan_id[5], ext_pan_id[6], ext_pan_id[7]);
    
    if (len > 0 && len < sizeof(ext_pan_id_str)) {
        mp_obj_dict_store(net_dict, MP_OBJ_NEW_QSTR(MP_QSTR_extended_pan_id), 
                         mp_obj_new_str(ext_pan_id_str, len));
    }
    
    return net_dict;
}

MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_get_network_info_obj, esp32_zig_get_network_info);

// update_network_status() - Updates the network status and refreshes network information
mp_obj_t esp32_zig_update_network_status(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Check if device is initialized and is a gateway
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is not initialized");
        return mp_const_none;
    }
    
    
    // Check if Zigbee stack is started
    bool is_started = esp_zb_is_started();
    
    // Update network state from Zigbee stack if it's started
    if (is_started) {
        uint16_t current_pan_id = esp_zb_get_pan_id();
        uint8_t current_channel = esp_zb_get_current_channel();
        
        // Network is considered formed if PAN ID is not 0 or 0xFFFF
        bool is_network_formed = (current_pan_id != 0 && current_pan_id != 0xFFFF);
        
        // Update data in the config object
        self->config->network_formed = is_network_formed;
        self->config->pan_id = current_pan_id;
        self->config->channel = current_channel;
        
        return mp_obj_new_bool(is_network_formed);
    } else {
        // If Zigbee stack is not started, network is not formed
        self->config->network_formed = false;
        self->config->pan_id = 0;
        self->config->channel = 0;
        
        return mp_const_false;
    }
}

MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_update_network_status_obj, esp32_zig_update_network_status);



// get_network_info() - Get information about the Zigbee network
static void scan_result_handler(esp_zb_zdp_status_t zdo_status, uint8_t count, esp_zb_network_descriptor_t *nwk_descriptor) {
    if (zdo_status != ESP_ZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGW("SCAN", "Scanning completed with error: %d", zdo_status);
        return;
    }
    ESP_LOGI("SCAN", "Scanning completed. Found networks: %d", count);
    for (int i = 0; i < count; ++i) {
        ESP_LOGI("SCAN", "Network %d: PAN ID: 0x%04x, Channel: %d, Permit Join: %d",
                 i + 1,
                 nwk_descriptor[i].short_pan_id,
                 nwk_descriptor[i].logic_channel,
                 nwk_descriptor[i].permit_joining);
    }
}





// scan_networks() - Scan for nearby Zigbee networks
static mp_obj_t esp32_zig_scan_networks(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Check if device is initialized and is a gateway
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Device is not initialized");
        return mp_const_none;
    }
    
    
    // Check if Zigbee stack is started
    if (!esp_zb_is_started()) {
        mp_raise_msg(&mp_type_RuntimeError, "Zigbee stack is not started");
        return mp_const_none;
    }
    
    // Start a network scan
    ZB_LOCK();
    // Scan all channels (11-26) with default duration
    uint32_t channel_mask = 0x07FFF800;  // All Zigbee channels (11-26)
    uint8_t scan_duration = 5;           // Reasonable duration for scanning
    esp_zb_zdo_active_scan_request(channel_mask, scan_duration, scan_result_handler);
    ZB_UNLOCK();
    
    // Note: This is an asynchronous operation, results will be delivered through the signal handler
    // For now, we'll just return a message that scan has started
    return mp_obj_new_str("Network scan started", 20);
}

MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_scan_networks_obj, esp32_zig_scan_networks);


// Python API: deferred start of Zigbee gateway
static mp_obj_t esp32_zig_start_network(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);

// Check if Zigbee stack is already running
    if (esp_zb_is_started()) {
        mp_raise_msg(&mp_type_RuntimeError, "Zigbee stack is already running");
        return mp_const_none;
    }

    ESP_LOGI("NET", "ZIGBEE: Starting Zigbee gateway commissioning ");
    esp_err_t err = esp32_zig_start_gateway(self);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, "Failed to start gateway: %s", esp_err_to_name(err));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_start_network_obj, esp32_zig_start_network);
