// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "device_manager.h"
#include "device_storage.h"
#include "py/obj.h" // For MP_OBJ_TO_PTR

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

// Modified device_manager_add to handle re-joins and use zig_obj_mp for storage
esp_err_t device_manager_add(uint16_t new_short_addr, const uint8_t ieee_addr[8], mp_obj_t zig_obj_mp, bool initial_load_context) {
    if (zig_obj_mp == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Zigbee object not provided to device_manager_add");
        return ESP_ERR_INVALID_ARG;
    }
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(zig_obj_mp);
    if (!self) { // Should not happen if zig_obj_mp is not none and is correct type
        ESP_LOGE(LOG_TAG, "Invalid Zigbee object pointer in device_manager_add");
        return ESP_ERR_INVALID_ARG;
    }

    zigbee_device_t *dev_by_ieee = device_manager_find_by_ieee(ieee_addr);

    if (dev_by_ieee != NULL) { // Device with this IEEE address is already known
        if (dev_by_ieee->short_addr == new_short_addr) { // Same IEEE, same short_addr - re-announce
            ESP_LOGI(LOG_TAG, "Device 0x%04x (IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x) re-announced.",
                     new_short_addr,
                     dev_by_ieee->ieee_addr[0], dev_by_ieee->ieee_addr[1], dev_by_ieee->ieee_addr[2], dev_by_ieee->ieee_addr[3],
                     dev_by_ieee->ieee_addr[4], dev_by_ieee->ieee_addr[5], dev_by_ieee->ieee_addr[6], dev_by_ieee->ieee_addr[7]);
            dev_by_ieee->active = true;
            device_manager_update_timestamp(new_short_addr);
            if (!initial_load_context) {
                ESP_LOGI(LOG_TAG, "Device 0x%04x re-announced. Saving state.", new_short_addr);
                device_storage_save(self, new_short_addr);
            } else {
                ESP_LOGI(LOG_TAG, "Device 0x%04x re-announced (during JSON load). Skipping immediate save.", new_short_addr);
            }
            return ESP_OK; // Indicate already exists, but updated
        } else { // Same IEEE, different short_addr - device re-joined
            uint16_t old_short_addr = dev_by_ieee->short_addr;
            ESP_LOGI(LOG_TAG, "Device with IEEE (%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x) re-joined. Old short_addr: 0x%04x, New short_addr: 0x%04x",
                     dev_by_ieee->ieee_addr[0], dev_by_ieee->ieee_addr[1], dev_by_ieee->ieee_addr[2], dev_by_ieee->ieee_addr[3],
                     dev_by_ieee->ieee_addr[4], dev_by_ieee->ieee_addr[5], dev_by_ieee->ieee_addr[6], dev_by_ieee->ieee_addr[7],
                     old_short_addr, new_short_addr);

            // Check if the new_short_addr is already taken by a *different* device
            zigbee_device_t *conflict_dev_on_new_short = device_manager_get(new_short_addr);
            if (conflict_dev_on_new_short != NULL && memcmp(conflict_dev_on_new_short->ieee_addr, ieee_addr, 8) != 0) {
                ESP_LOGE(LOG_TAG, "New short_addr 0x%04x for re-joining device (ATTEMPTED IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x) is already taken by a different device (EXISTING IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x).",
                         new_short_addr,
                         ieee_addr[0], ieee_addr[1], ieee_addr[2], ieee_addr[3],
                         ieee_addr[4], ieee_addr[5], ieee_addr[6], ieee_addr[7],
                         conflict_dev_on_new_short->ieee_addr[0], conflict_dev_on_new_short->ieee_addr[1], conflict_dev_on_new_short->ieee_addr[2], conflict_dev_on_new_short->ieee_addr[3],
                         conflict_dev_on_new_short->ieee_addr[4], conflict_dev_on_new_short->ieee_addr[5], conflict_dev_on_new_short->ieee_addr[6], conflict_dev_on_new_short->ieee_addr[7]);
                return ESP_ERR_INVALID_STATE; // Address collision
            }

            // Proceed to remove old entry
            device_storage_remove(self, old_short_addr);
            device_manager_remove(old_short_addr); // This will remove dev_by_ieee from the list
            // Fall through to add the device with new_short_addr
        }
    } else { // dev_by_ieee is NULL - device with this IEEE is not known yet
        // Check if new_short_addr is already taken by any device
        zigbee_device_t *dev_on_new_short = device_manager_get(new_short_addr);
        if (dev_on_new_short != NULL) {
            // This means new_short_addr is taken by a device with a *different* IEEE (otherwise dev_by_ieee would have found it)
            ESP_LOGE(LOG_TAG, "Short_addr 0x%04x for new device (ATTEMPTED IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x) is already taken by device with (EXISTING IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x).",
                     new_short_addr,
                     ieee_addr[0], ieee_addr[1], ieee_addr[2], ieee_addr[3],
                     ieee_addr[4], ieee_addr[5], ieee_addr[6], ieee_addr[7],
                     dev_on_new_short->ieee_addr[0], dev_on_new_short->ieee_addr[1], dev_on_new_short->ieee_addr[2], dev_on_new_short->ieee_addr[3],
                     dev_on_new_short->ieee_addr[4], dev_on_new_short->ieee_addr[5], dev_on_new_short->ieee_addr[6], dev_on_new_short->ieee_addr[7]);
            return ESP_ERR_INVALID_STATE; // Address collision
        }
    }

    // At this point, either the device is genuinely new, or it re-joined and its old entry was removed.
    // And new_short_addr is confirmed to be free or was freed up for this IEEE.

    if (device_list.device_count >= MAX_DEVICES) {
        ESP_LOGE(LOG_TAG, "Device list is full. Cannot add device 0x%04x.", new_short_addr);
        return ESP_ERR_NO_MEM;
    }

    zigbee_device_t *new_device_entry = &device_list.devices[device_list.device_count++];
    memset(new_device_entry, 0, sizeof(zigbee_device_t));

    new_device_entry->short_addr = new_short_addr;
    memcpy(new_device_entry->ieee_addr, ieee_addr, 8);
    new_device_entry->active = true;
    new_device_entry->last_seen = esp_timer_get_time() / 1000; // Current time in ms

    ESP_LOGI(LOG_TAG, "Added device 0x%04x (IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)", new_short_addr,
             new_device_entry->ieee_addr[0], new_device_entry->ieee_addr[1], new_device_entry->ieee_addr[2], new_device_entry->ieee_addr[3],
             new_device_entry->ieee_addr[4], new_device_entry->ieee_addr[5], new_device_entry->ieee_addr[6], new_device_entry->ieee_addr[7]);
    
    if (!initial_load_context) {
        ESP_LOGI(LOG_TAG, "New device 0x%04x added. Saving state.", new_short_addr);
        device_storage_save(self, new_short_addr);
    } else {
        ESP_LOGI(LOG_TAG, "New device 0x%04x added (during JSON load). Skipping immediate save.", new_short_addr);
    }
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
