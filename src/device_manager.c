// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include <stdio.h> // Required for snprintf
#include "esp_log.h"
#include "esp_timer.h"
#include "device_manager.h"
#include "device_storage.h"
#include "py/obj.h" // For MP_OBJ_TO_PTR
#include "mod_zig_core.h" // For zigbee_format_ieee_addr_to_str

#define LOG_TAG "DEVICE_MANAGER"

static zigbee_device_list_t device_list = {0};



// Helper function for finding endpoint
static int find_endpoint(zigbee_device_t *device, uint8_t endpoint) {
    for (int i = 0; i < device->endpoint_count; i++) {
        if (device->endpoints[i].endpoint == endpoint) {
            return i;
        }
    }
    return -1;
}

// Helper function to find a device by its IEEE address
static zigbee_device_t* device_manager_find_by_ieee(const uint8_t ieee_addr[8]) {
    for (int i = 0; i < device_list.device_count; i++) {
        if (memcmp(device_list.devices[i].ieee_addr, ieee_addr, 8) == 0) {
            return &device_list.devices[i];
        }
    }
    return NULL;
}

esp_err_t device_manager_init(void) {
    device_list.device_count = 0;
    return ESP_OK;
}




// Internal helper: create a new device entry unconditionally
static esp_err_t _create_device_internal(uint16_t new_short_addr, const uint8_t ieee_addr[8], esp32_zig_obj_t *self) {
    if (device_list.device_count >= MAX_DEVICES) {
        ESP_LOGE(LOG_TAG, "Add device failed: list full. Cannot add 0x%04x", new_short_addr);
        return ESP_ERR_NO_MEM;
    }
    zigbee_device_t *new_dev = &device_list.devices[device_list.device_count];
    memset(new_dev, 0, sizeof(zigbee_device_t));
    new_dev->short_addr = new_short_addr;
    memcpy(new_dev->ieee_addr, ieee_addr, sizeof(new_dev->ieee_addr));
    zigbee_format_ieee_addr_to_str(new_dev->ieee_addr, new_dev->ieee_addr_str, sizeof(new_dev->ieee_addr_str));
    new_dev->active = true;
    new_dev->last_seen = esp_timer_get_time() / 1000;
    new_dev->endpoint_count = 0;
    device_list.device_count++;
    ESP_LOGI(LOG_TAG, "Added new device: Short=0x%04x, IEEE=%s. Count: %d", new_short_addr, new_dev->ieee_addr_str, device_list.device_count);
    if (self && self->storage_cb != mp_const_none) {
        device_storage_save(self, new_short_addr);
    }
    return ESP_OK;
}

// Modified device_manager_add to handle generic add/update
esp_err_t device_manager_add(uint16_t new_short_addr, const uint8_t ieee_addr[8], mp_obj_t zig_obj_mp) {
    esp32_zig_obj_t *self = NULL;
    if (zig_obj_mp != mp_const_none) {
        self = MP_OBJ_TO_PTR(zig_obj_mp);
    }

    zigbee_device_t *device = device_manager_find_by_ieee(ieee_addr);
    if (device) {
        // Update existing device
        if (device->short_addr != new_short_addr) {
            zigbee_device_t *conflict = device_manager_get(new_short_addr);
            if (conflict && conflict != device) {
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_remove(self, conflict->short_addr);
                }
                device_manager_remove(conflict->short_addr);
            }
            device->short_addr = new_short_addr;
        }
        device->active = true;
        device_manager_update_timestamp(new_short_addr);
        if (self && self->storage_cb != mp_const_none) {
            device_storage_save(self, new_short_addr);
        }
        return ESP_OK;
    }

    // Add new device via internal helper
    return _create_device_internal(new_short_addr, ieee_addr, self);
}

/**
 * @brief Add device without persisting to storage (for initial JSON load)
 */
esp_err_t device_manager_add_new_device(uint16_t new_short_addr, const uint8_t ieee_addr[8], mp_obj_t zig_obj_mp) {
    (void)zig_obj_mp; // skip storage callback for JSON loading
    return _create_device_internal(new_short_addr, ieee_addr, NULL);
}

esp_err_t device_manager_remove(uint16_t short_addr) {
    // Find device
    int idx = -1;
    for (int i = 0; i < device_list.device_count; i++) {
        if (device_list.devices[i].short_addr == short_addr) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        ESP_LOGW(LOG_TAG, "Device 0x%04x not found", short_addr);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining devices
    for (int i = idx; i < device_list.device_count - 1; i++) {
        memcpy(&device_list.devices[i], 
               &device_list.devices[i + 1], 
               sizeof(zigbee_device_t));
    }
    
    // Clear the last device slot and decrement count
    memset(&device_list.devices[device_list.device_count - 1], 0, sizeof(zigbee_device_t));
    device_list.device_count--;
    ESP_LOGI(LOG_TAG, "Removed device 0x%04x", short_addr);
    return ESP_OK;
}

esp_err_t device_manager_update(const zigbee_device_t *update) {
    if (!update) {
        return ESP_ERR_INVALID_ARG;
    }

    zigbee_device_t *device = device_manager_get(update->short_addr);
    if (!device) {
        ESP_LOGW(LOG_TAG, "Device 0x%04x not found for update", update->short_addr);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(LOG_TAG, "Updating data for device 0x%04x. Current IEEE in struct: %s. Active: %d. Name: '%s'. Manu: '%s'. Model: '%s'.", 
             device->short_addr, 
             device->ieee_addr_str, 
             device->active,
             device->device_name,
             device->manufacturer_name,
             device->model); // Log key fields before update copy

// Update only basic device fields, not touching endpoints
    device->short_addr = update->short_addr; // This should not change if device was fetched by update->short_addr
    memcpy(device->ieee_addr, update->ieee_addr, sizeof(device->ieee_addr));
    zigbee_format_ieee_addr_to_str(device->ieee_addr, device->ieee_addr_str, sizeof(device->ieee_addr_str));
    device->active = update->active;
    // Ensure null termination for string copies from 'update' if they come from potentially unsafe sources
    strncpy(device->device_name, update->device_name, sizeof(device->device_name) - 1);
    device->device_name[sizeof(device->device_name) - 1] = '\0';
    
    device->manufacturer_code = update->manufacturer_code;
    
    strncpy(device->manufacturer_name, update->manufacturer_name, sizeof(device->manufacturer_name) - 1);
    device->manufacturer_name[sizeof(device->manufacturer_name) - 1] = '\0';
    
    strncpy(device->model, update->model, sizeof(device->model) - 1);
    device->model[sizeof(device->model) - 1] = '\0';
    
// Process new endpoints
    for (int i = 0; i < update->endpoint_count; i++) {
        uint8_t ep = update->endpoints[i].endpoint;
        int existing_idx = find_endpoint(device, ep);
        
        if (existing_idx >= 0) {
// Update existing endpoint
            device->endpoints[existing_idx] = update->endpoints[i];
        } else if (device->endpoint_count < MAX_ENDPOINTS) {
// Add new endpoint
            device->endpoints[device->endpoint_count++] = update->endpoints[i];
        }
    }

    device_manager_update_timestamp(device->short_addr);
    ESP_LOGI(LOG_TAG, "Device update processed for short_addr=0x%04x. IEEE after update: %s.", 
             device->short_addr,
             device->ieee_addr_str);
    return ESP_OK;
}

zigbee_device_t* device_manager_get(uint16_t short_addr) {
    for (int i = 0; i < device_list.device_count; i++) {
        if (device_list.devices[i].short_addr == short_addr) {
            return &device_list.devices[i];
        }
    }
    return NULL;
}

bool device_manager_is_available(uint16_t short_addr) {
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) return false;
    
    uint32_t now = esp_timer_get_time() / 1000;
    uint32_t timeout = 3600000; // 1 hour timeout
    
    return device->active && ((now - device->last_seen) < timeout);
}

void device_manager_update_timestamp(uint16_t short_addr) {
    zigbee_device_t *device = device_manager_get(short_addr);
    if (device) {
        device->last_seen = esp_timer_get_time() / 1000;
    }
}

zigbee_device_t* device_manager_get_list(size_t *count) {
    if (count) {
        *count = device_list.device_count;
    }
    return device_list.devices;
}
