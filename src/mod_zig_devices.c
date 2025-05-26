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

// Initialize device manager function
bool init_device_manager(void) {
    esp_err_t err = device_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to init device manager: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// High-level device management functions
esp_err_t add_device_to_list(esp32_zig_obj_t *self, uint16_t short_addr, const uint8_t ieee_addr[8], bool initial_load_context) {
    esp_err_t err = device_manager_add(short_addr, ieee_addr, MP_OBJ_FROM_PTR(self), initial_load_context);
    return err;
}

esp_err_t remove_device_from_list(esp32_zig_obj_t *self, uint16_t short_addr) {
    esp_err_t err = device_manager_remove(short_addr);
    if (err == ESP_OK) {
        device_storage_remove(self, short_addr);
    }
    return err;
}

esp_err_t update_device_info(esp32_zig_obj_t *self, zigbee_device_t *device) {
    zigbee_device_t *existing_device = device_manager_get(device->short_addr);
    if (existing_device != NULL) {
        // Update device info
        device_manager_update(device);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

// Helper function for getting text description of link quality
static const char* get_quality_description(uint8_t lqi) {
    if (lqi >= 200) return "Very Good";
    else if (lqi >= 150) return "Good";
    else if (lqi >= 100) return "Medium";
    else return "Bad";
}

void device_update_link_quality(zigbee_device_t *device, const void *info_ptr) {
    if (!device || !info_ptr) return;
    
// Using a simple structure to access LQI and RSSI
    typedef struct {
        uint8_t lqi;
        int8_t rssi;
    } link_quality_info_t;
    
    const link_quality_info_t *info = (const link_quality_info_t *)info_ptr;
    
// Update only if values have changed
    if (device->last_lqi != info->lqi || device->last_rssi != info->rssi) {
        device->last_lqi = info->lqi;
        device->last_rssi = info->rssi;
        
        ESP_LOGI(LOG_TAG, "Device 0x%04x signal quality updated: LQI=%d (%s), RSSI=%ddBm", 
                 device->short_addr, info->lqi, get_quality_description(info->lqi), info->rssi);
    }
}

uint8_t device_get_link_quality(zigbee_device_t *device) {
    return device ? device->last_lqi : 0;
}

const char* device_get_link_quality_description(zigbee_device_t *device) {
    return device ? get_quality_description(device->last_lqi) : "Unknown";
}

