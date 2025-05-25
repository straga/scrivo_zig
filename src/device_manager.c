// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "device_manager.h"
#include "device_storage.h"

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

esp_err_t device_manager_init(void) {
    device_list.device_count = 0;
    return ESP_OK;
}

esp_err_t device_manager_add(uint16_t short_addr, uint64_t ieee_addr) {
    // Check if device already exists
    if (device_manager_get(short_addr) != NULL) {
        ESP_LOGW(LOG_TAG, "Device 0x%04x already exists", short_addr);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check space
    if (device_list.device_count >= MAX_DEVICES) {
        ESP_LOGE(LOG_TAG, "Device list is full");
        return ESP_ERR_NO_MEM;
    }
    
    // Add new device
    zigbee_device_t *device = &device_list.devices[device_list.device_count++];
    memset(device, 0, sizeof(zigbee_device_t));
    
    device->short_addr = short_addr;
    memcpy(device->ieee_addr, &ieee_addr, sizeof(device->ieee_addr));
    device->active = true;
    device->last_seen = esp_timer_get_time() / 1000;
    
    ESP_LOGI(LOG_TAG, "Added device 0x%04x", short_addr);
    return ESP_OK;
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

// Update only basic device fields, not touching endpoints
    device->short_addr = update->short_addr;
    memcpy(device->ieee_addr, update->ieee_addr, sizeof(device->ieee_addr));
    device->active = update->active;
    memcpy(device->device_name, update->device_name, sizeof(device->device_name));
    device->manufacturer_code = update->manufacturer_code;
    memcpy(device->manufacturer_name, update->manufacturer_name, sizeof(device->manufacturer_name));
    memcpy(device->model, update->model, sizeof(device->model));
    
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
    ESP_LOGI(LOG_TAG, "Updated device 0x%04x", device->short_addr);
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
