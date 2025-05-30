// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/gc.h"
#include "py/mpstate.h"
#include "esp_log.h"
#include "esp_err.h"

#include "mod_zig_devices.h"
#include "device_manager.h"
#include "device_storage.h"
#include "device_json.h"

// ESP-Zigbee headers for structure definitions
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"

#define LOG_TAG "MOD_ZIG_DEVICES"

// Python API functions for device management

// Save single device
mp_obj_t esp32_zig_save_device(size_t n_args, const mp_obj_t *args) {
    if (n_args != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("save_device requires device short address"));
        return mp_const_none;
    }
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t short_addr = mp_obj_get_int(args[1]);
    
    esp_err_t err = device_storage_save(self, short_addr);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, 
                         MP_ERROR_TEXT("Failed to save device: %s"), 
                         esp_err_to_name(err));
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_save_device_obj, 2, 2, esp32_zig_save_device);

// Remove single device
mp_obj_t esp32_zig_remove_device(size_t n_args, const mp_obj_t *args) {
    if (n_args != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("remove_device requires device short address"));
        return mp_const_none;
    }
    
    uint16_t short_addr = mp_obj_get_int(args[1]);
    esp_err_t err = device_manager_remove(short_addr);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, 
                         MP_ERROR_TEXT("Failed to remove device: %s"), 
                         esp_err_to_name(err));
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_remove_device_obj, 2, 2, esp32_zig_remove_device);

// Load single device
mp_obj_t esp32_zig_load_device(size_t n_args, const mp_obj_t *args) {
    if (n_args != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("load_device requires device short address"));
        return mp_const_none;
    }
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t short_addr = mp_obj_get_int(args[1]);
    
    esp_err_t err = device_storage_load(self, short_addr);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, 
                         MP_ERROR_TEXT("Failed to load device: %s"), 
                         esp_err_to_name(err));
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_load_device_obj, 2, 2, esp32_zig_load_device);

// Get device info
mp_obj_t esp32_zig_get_device(size_t n_args, const mp_obj_t *args) {
    if (n_args != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("get_device requires device short address"));
        return mp_const_none;
    }
    
    uint16_t short_addr = mp_obj_get_int(args[1]);
    zigbee_device_t *device = device_manager_get(short_addr);
    
    if (!device) {
        return mp_const_none;
    }
    
    // Create JSON object with device info
    cJSON *json = device_to_json(device);
    if (!json) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to create device JSON"));
        return mp_const_none;
    }
    
    // Convert to string
    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_str) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to convert JSON to string"));
        return mp_const_none;
    }
    
    // Create Python string
    mp_obj_t ret = mp_obj_new_str(json_str, strlen(json_str));
    free(json_str);
    
    return ret;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_get_device_obj, 2, 2, esp32_zig_get_device);

// Get list of all devices (only short addresses)
mp_obj_t esp32_zig_get_device_list(size_t n_args, const mp_obj_t *args) {
    if (n_args != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("get_device_list takes no arguments"));
        return mp_const_none;
    }
    size_t count;
    zigbee_device_t *devices = device_manager_get_list(&count);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < count; i++) {
        mp_obj_list_append(list, mp_obj_new_int(devices[i].short_addr));
    }
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_get_device_list_obj, 1, 1, esp32_zig_get_device_list);

// Get summary info for device: selected fields only
mp_obj_t esp32_zig_get_device_summary(size_t n_args, const mp_obj_t *args) {
    if (n_args != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("get_device_summary requires device short address"));
        return mp_const_none;
    }
    uint16_t short_addr = mp_obj_get_int(args[1]);
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        return mp_const_none;
    }
    // Build JSON with selected fields
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "last_seen", device->last_seen);
    cJSON_AddStringToObject(json, "ieee", device->ieee_addr_str);
    cJSON_AddStringToObject(json, "manuf_name", device->manufacturer_name);
    cJSON_AddStringToObject(json, "model", device->model);
    cJSON_AddStringToObject(json, "name", device->device_name);
    cJSON_AddBoolToObject(json, "active", device->active);
    cJSON_AddNumberToObject(json, "frm_ver", device->firmware_version);
    cJSON_AddNumberToObject(json, "power", device->power_source);
    cJSON_AddNumberToObject(json, "bat_volt", device->battery_voltage);
    cJSON_AddNumberToObject(json, "bat_perc", device->battery_percentage);
    cJSON_AddNumberToObject(json, "manuf_code", device->manufacturer_code);
    cJSON_AddNumberToObject(json, "prod_ver", device->prod_config_version);
    cJSON_AddNumberToObject(json, "lqi", device->last_lqi);
    cJSON_AddNumberToObject(json, "rssi", device->last_rssi);
    // Convert to string
    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    mp_obj_t ret = mp_obj_new_str(json_str, strlen(json_str));
    free(json_str);
    return ret;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_get_device_summary_obj, 2, 2, esp32_zig_get_device_summary);

// Helper function for getting text description of link quality
static const char* get_quality_description(uint8_t lqi) {
    if (lqi >= 200) return "Very Good";
    else if (lqi >= 150) return "Good";
    else if (lqi >= 100) return "Medium";
    else return "Bad";
}

void device_update_link_quality(zigbee_device_t *device, const void *info_ptr) {
    if (!device || !info_ptr) return;
    
    // Since we cannot reliably access LQI and RSSI from ZCL command messages
    // (these fields are not consistently available in all message types),
    // we'll implement a simple approach that just marks the device as active
    // when this function is called, indicating recent communication.
    
    // The info_ptr parameter is kept for future extensibility when 
    // proper network quality information becomes available through
    // different callback mechanisms.
    
    // Mark device as active since we received a message from it
    device->active = true;
    
    ESP_LOGD(LOG_TAG, "Device 0x%04x marked as active (recent communication)", 
             device->short_addr);
    
    // Note: In the future, this function should be called from contexts where
    // actual LQI/RSSI information is available, such as:
    // - Network layer callbacks with esp_zb_apsde_data_ind_t
    // - Neighbor table updates with esp_zb_nwk_neighbor_info_t
    // - Custom network monitoring functions
}

uint8_t device_get_link_quality(zigbee_device_t *device) {
    return device ? device->last_lqi : 0;
}

const char* device_get_link_quality_description(zigbee_device_t *device) {
    return device ? get_quality_description(device->last_lqi) : "Unknown";
}

