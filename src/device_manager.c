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

// Helper function to format IEEE address into a string
static void _format_device_ieee_str(zigbee_device_t *device) {
    if (device) {
        snprintf(device->ieee_addr_str, sizeof(device->ieee_addr_str),
                 "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                 device->ieee_addr[0], device->ieee_addr[1], device->ieee_addr[2], device->ieee_addr[3],
                 device->ieee_addr[4], device->ieee_addr[5], device->ieee_addr[6], device->ieee_addr[7]);
    }
}

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
    if (zig_obj_mp == mp_const_none && !initial_load_context) {
        ESP_LOGE(LOG_TAG, "Zigbee object not provided to device_manager_add for a network event");
        return ESP_ERR_INVALID_ARG;
    }
    esp32_zig_obj_t *self = NULL;
    if (zig_obj_mp != mp_const_none) {
        self = MP_OBJ_TO_PTR(zig_obj_mp);
    }

    bool add_new_entry_to_list = false;

    if (initial_load_context) {
        char initial_log_ieee_str[24];
        zigbee_format_ieee_addr_to_str(ieee_addr, initial_log_ieee_str, sizeof(initial_log_ieee_str));
        ESP_LOGI(LOG_TAG, "JSON LOAD (device_manager_add entry): new_short_addr=0x%04x, ieee_addr=%s", new_short_addr, initial_log_ieee_str);

        zigbee_device_t *dev_by_ieee = device_manager_find_by_ieee(ieee_addr); 
        zigbee_device_t *dev_on_new_short = device_manager_get(new_short_addr); 

        if (dev_by_ieee != NULL) {
            ESP_LOGI(LOG_TAG, "JSON LOAD: Found existing device by IEEE %s. Its current short_addr is 0x%04x.", 
                     dev_by_ieee->ieee_addr_str, dev_by_ieee->short_addr);
            if (dev_by_ieee->short_addr == new_short_addr) {
                ESP_LOGI(LOG_TAG, "JSON LOAD: IEEE %s already in manager with the same short_addr 0x%04x. No change to short_addr needed.", 
                         dev_by_ieee->ieee_addr_str, new_short_addr);
                // Device already exists with the same short_addr and IEEE. Mark as active, update last_seen.
                dev_by_ieee->active = true;
                dev_by_ieee->last_seen = esp_timer_get_time() / 1000;
                // No need to add to list, it's already there. device_from_json will call device_manager_update later.
                return ESP_OK; // Indicate that it's handled, no new entry needed
            } else {
                ESP_LOGI(LOG_TAG, "JSON LOAD: IEEE %s exists with different short_addr 0x%04x (JSON wants 0x%04x). Checking target short_addr.", 
                         dev_by_ieee->ieee_addr_str, dev_by_ieee->short_addr, new_short_addr);
                if (dev_on_new_short != NULL && dev_on_new_short != dev_by_ieee) {
                    char conflict_ieee_str[24];
                    zigbee_format_ieee_addr_to_str(dev_on_new_short->ieee_addr, conflict_ieee_str, sizeof(conflict_ieee_str));
                    ESP_LOGW(LOG_TAG, "JSON LOAD: Conflict! Target short_addr 0x%04x for IEEE %s is already occupied by a DIFFERENT device (IEEE %s, short_addr 0x%04x). Cannot move IEEE %s to 0x%04x.", 
                             new_short_addr, initial_log_ieee_str, conflict_ieee_str, dev_on_new_short->short_addr, initial_log_ieee_str, new_short_addr);
                    // Cannot update short_addr for dev_by_ieee. It remains with its old short_addr.
                    // The JSON entry for new_short_addr might be effectively ignored if it was meant for dev_by_ieee.
                    // Or, if this JSON entry was for a *new* device that happens to have a short_addr used by someone else's IEEE. 
                    // This case should be rare if short_addr are unique per IEEE in JSONs.
                    return ESP_ERR_INVALID_STATE; // Indicate a conflict that prevents applying this JSON's short_addr to this IEEE
                } else {
                    ESP_LOGI(LOG_TAG, "JSON LOAD: Target short_addr 0x%04x is free or occupied by the same IEEE. Updating short_addr for IEEE %s from 0x%04x to 0x%04x.", 
                             new_short_addr, dev_by_ieee->ieee_addr_str, dev_by_ieee->short_addr, new_short_addr);
                    // Record old short_addr for JSON removal
                    uint16_t old_short_addr = dev_by_ieee->short_addr;
                    // Update the short_addr of the existing device entry
                    dev_by_ieee->short_addr = new_short_addr;
                    _format_device_ieee_str(dev_by_ieee);
                    dev_by_ieee->active = true;
                    dev_by_ieee->last_seen = esp_timer_get_time() / 1000;
                    // Remove old JSON file now that we've moved this device to new_short_addr
                    if (self && self->storage_cb != mp_const_none) {
                        device_storage_remove(self, old_short_addr);
                    }
                    return ESP_OK; // Indicate that it's handled, no new entry needed
                }
            }
        } else { // dev_by_ieee == NULL (No device with this IEEE address currently in the list)
            ESP_LOGI(LOG_TAG, "JSON LOAD: IEEE %s not found in manager. Checking if target short_addr 0x%04x is occupied.", 
                     initial_log_ieee_str, new_short_addr);
            if (dev_on_new_short != NULL) {
                // Target short_addr is occupied by a device with a DIFFERENT IEEE.
                char conflict_ieee_str[24];
                zigbee_format_ieee_addr_to_str(dev_on_new_short->ieee_addr, conflict_ieee_str, sizeof(conflict_ieee_str));
                ESP_LOGW(LOG_TAG, "JSON LOAD: Conflict! Cannot add new IEEE %s with short_addr 0x%04x because that short_addr is already occupied by a DIFFERENT device (IEEE %s).", 
                         initial_log_ieee_str, new_short_addr, conflict_ieee_str);
                return ESP_ERR_INVALID_STATE; // Indicate a conflict
            } else {
                ESP_LOGI(LOG_TAG, "JSON LOAD: IEEE %s not found, and short_addr 0x%04x is free. Will add as a new entry.", 
                         initial_log_ieee_str, new_short_addr);
                add_new_entry_to_list = true;
            }
        }
    } else {
        // --- Logic for REAL NETWORK EVENTS (more assertive) ---
        // Temporary buffer for logging if device object for ieee_addr might not be self or dev_by_ieee yet
        char log_ieee_str[24];
        zigbee_format_ieee_addr_to_str(ieee_addr, log_ieee_str, sizeof(log_ieee_str));

        if (!self || self->storage_cb == mp_const_none) {
             ESP_LOGW(LOG_TAG, "NET EVENT: storage_cb not available. Device 0x%04x (IEEE: %s) changes will not be persisted.", new_short_addr, log_ieee_str);
        }

        zigbee_device_t *conflict_dev_on_new_short = device_manager_get(new_short_addr);

        zigbee_device_t *dev_by_ieee = device_manager_find_by_ieee(ieee_addr);

        if (dev_by_ieee != NULL) { // Device with this IEEE already exists in memory
            ESP_LOGI(LOG_TAG, "NET EVENT: Found existing device by IEEE %s. In-memory short_addr: 0x%04x. Event new_short_addr: 0x%04x",
                     dev_by_ieee->ieee_addr_str, dev_by_ieee->short_addr, new_short_addr);
            if (dev_by_ieee->short_addr == new_short_addr) { // Re-announce
                ESP_LOGI(LOG_TAG, "NET EVENT: Device 0x%04x (IEEE: %s) re-announced.",
                         new_short_addr, dev_by_ieee->ieee_addr_str);
                dev_by_ieee->active = true;
                device_manager_update_timestamp(new_short_addr);
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_save(self, new_short_addr);
                }
                // If new_short_addr was supposedly taken by another device, but this is a re-announce for dev_by_ieee,
                // that other conflicting device (if conflict_dev_on_new_short existed and was not dev_by_ieee) should be removed.
                if (conflict_dev_on_new_short != NULL && conflict_dev_on_new_short != dev_by_ieee) {
                    ESP_LOGW(LOG_TAG, "NET RE-ANNOUNCE: Short addr 0x%04x used by re-announcing device (IEEE: %s) was also assigned to another stored device (IEEE: %s). Removing the other stored device.",
                             new_short_addr, 
                             dev_by_ieee->ieee_addr_str,
                             conflict_dev_on_new_short->ieee_addr_str);
                    if (self && self->storage_cb != mp_const_none) {
                        // Use the short_addr of the conflicting device for removal from storage
                        device_storage_remove(self, conflict_dev_on_new_short->short_addr); 
                    }
                    device_manager_remove(conflict_dev_on_new_short->short_addr); // Remove from memory
                }

            } else { // Real Re-join: same IEEE (dev_by_ieee), different short_addr (new_short_addr)
                uint16_t old_short_addr_in_mem = dev_by_ieee->short_addr;
                ESP_LOGI(LOG_TAG, "NET EVENT: Device with IEEE %s re-joined. Old short_addr: 0x%04x, New short_addr: 0x%04x.",
                         dev_by_ieee->ieee_addr_str, old_short_addr_in_mem, new_short_addr);

                // If new_short_addr is taken by a COMPLETELY DIFFERENT device, remove that other device.
                if (conflict_dev_on_new_short != NULL && conflict_dev_on_new_short != dev_by_ieee) {
                    ESP_LOGW(LOG_TAG, "NET RE-JOIN: New short_addr 0x%04x for re-joining device (IEEE: %s) was assigned to another stored device (IEEE: %s). Removing the other stored device.",
                             new_short_addr, 
                             dev_by_ieee->ieee_addr_str,
                             conflict_dev_on_new_short->ieee_addr_str);
                    if (self && self->storage_cb != mp_const_none) {
                        device_storage_remove(self, conflict_dev_on_new_short->short_addr);
                    }
                    device_manager_remove(conflict_dev_on_new_short->short_addr); // Remove from memory
                }
                
                // Remove old JSON for the re-joining device
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_remove(self, old_short_addr_in_mem);
                }

                // Update the existing in-memory entry (dev_by_ieee) to the new_short_addr
                dev_by_ieee->short_addr = new_short_addr;
                dev_by_ieee->active = true;
                device_manager_update_timestamp(new_short_addr);
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_save(self, new_short_addr);
                }
            }
        } else { // dev_by_ieee == NULL (truly new device for the network for this IEEE)
            ESP_LOGI(LOG_TAG, "NET EVENT: New device (IEEE %s not found in memory) joining with short_addr 0x%04x.",
                     log_ieee_str, new_short_addr);
            // If new_short_addr is taken by a COMPLETELY DIFFERENT device, remove that other device.
            if (conflict_dev_on_new_short != NULL) { 
                 ESP_LOGW(LOG_TAG, "NET NEW JOIN: New short_addr 0x%04x for joining device (IEEE: %s) was assigned to another stored device (IEEE: %s). Removing the other stored device.",
                             new_short_addr, 
                             log_ieee_str,
                             conflict_dev_on_new_short->ieee_addr_str);
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_remove(self, conflict_dev_on_new_short->short_addr);
                }
                device_manager_remove(conflict_dev_on_new_short->short_addr); // Remove from memory
            }
            add_new_entry_to_list = true; // Proceed to add the new device
        }
    }

    if (add_new_entry_to_list) {
        // This section adds (new_short_addr, ieee_addr) to the device_list.
        // It's reached if:
        // 1. JSON LOAD: IEEE not in memory OR same IEEE in memory but with different short_addr.
        // 2. NET EVENT: IEEE not in memory (truly new device) AND new_short_addr was either free or freed.

        // We re-check conflict_dev_on_new_short because it might have been removed in the NET EVENT block above.
        zigbee_device_t *current_holder_of_new_short_addr = device_manager_get(new_short_addr);
        if (current_holder_of_new_short_addr != NULL) {
            // This means new_short_addr is still taken. This should only happen during JSON LOAD if an
            // entry (new_short_addr, same_ieee_addr) was already processed from another JSON file.
            if (memcmp(current_holder_of_new_short_addr->ieee_addr, ieee_addr, 8) == 0) {
                ESP_LOGI(LOG_TAG, "Device 0x%04x (IEEE: %s) already present. Skipping duplicate add to list.",
                         new_short_addr, current_holder_of_new_short_addr->ieee_addr_str);
                current_holder_of_new_short_addr->active = true; 
                device_manager_update_timestamp(new_short_addr);
            } else {
                // This case implies new_short_addr is taken by a different IEEE.
                // For JSON LOAD, we log this as a warning and don't add, to prevent list corruption without a clear resolution path here.
                // For NET EVENT, this state should have been resolved by the assertive logic above (conflict_dev_on_new_short would have been removed).
                // If it's still taken by a different IEEE in NET_EVENT, it's an unexpected state.
                // Use temp_ieee_str for the "ATTEMPTED" one, as it's not yet in a device struct if adding new
                char temp_attempted_ieee_str[24];
                zigbee_format_ieee_addr_to_str(ieee_addr, temp_attempted_ieee_str, sizeof(temp_attempted_ieee_str));
                ESP_LOGE(LOG_TAG, "%s: Cannot add device 0x%04x (ATTEMPTED IEEE: %s): short_addr is STILL taken by a different device (EXISTING IEEE: %s).",
                         initial_load_context ? "JSON LOAD WARNING" : "NET EVENT CRITICAL",
                         new_short_addr, 
                         temp_attempted_ieee_str,
                         current_holder_of_new_short_addr->ieee_addr_str);
                 if(!initial_load_context) return ESP_ERR_INVALID_STATE; // For NET EVENT, treat as error.
            }
        } else {
            // `new_short_addr` is free. Proceed to add.
            if (device_list.device_count >= MAX_DEVICES) {
                ESP_LOGE(LOG_TAG, "Device list is full. Cannot add device 0x%04x.", new_short_addr);
                return ESP_ERR_NO_MEM;
            }
            zigbee_device_t *new_device_entry = &device_list.devices[device_list.device_count++];
            memset(new_device_entry, 0, sizeof(zigbee_device_t));
            new_device_entry->short_addr = new_short_addr;
            memcpy(new_device_entry->ieee_addr, ieee_addr, 8);
            _format_device_ieee_str(new_device_entry); // Format and store the string version
            new_device_entry->active = true;
            device_manager_update_timestamp(new_short_addr);

            ESP_LOGI(LOG_TAG, "Added new entry to device list: 0x%04x (IEEE: %s). List count: %d",
                     new_short_addr, new_device_entry->ieee_addr_str, device_list.device_count);

            if (!initial_load_context) { // Only save if it was a network event that added this new entry
                if (self && self->storage_cb != mp_const_none) {
                    device_storage_save(self, new_short_addr);
                }
            }
        }
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
    _format_device_ieee_str(device); // Update the formatted string whenever ieee_addr bytes change
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
    ESP_LOGI(LOG_TAG, "Device update processed for short_addr=0x%04x. IEEE after update: %s. (DM_UPDATE_COMPLETE)", 
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
