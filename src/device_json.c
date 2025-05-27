// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "device_json.h"
#include "device_manager.h"
#include "mod_zig_core.h" // For zigbee_format_ieee_addr_to_str and zigbee_parse_ieee_str_to_addr

#define LOG_TAG "DEVICE_JSON"

// Function for cleaning control characters from string
static void clean_string(char *str) {
    if (!str) return;
    
// Skip leading control characters
    char *src = str;
    while (*src && !isprint((unsigned char)*src)) src++;
    
// Copy only printable characters
    char *dst = str;
    while (*src) {
        if (isprint((unsigned char)*src)) {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';
}

// Convert device to JSON object
cJSON* device_to_json(const zigbee_device_t *device) {
    if (!device) {
        ESP_LOGE(LOG_TAG, "NULL device pointer");
        return NULL;
    }

    ESP_LOGI(LOG_TAG, "Creating JSON for device 0x%04x", device->short_addr);
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(LOG_TAG, "Failed to create JSON object");
        return NULL;
    }
    
    // Add basic device info - short address as hex string
    char short_addr_str[8];
    snprintf(short_addr_str, sizeof(short_addr_str), "0x%04x", device->short_addr);
    cJSON_AddStringToObject(json, "short_addr", short_addr_str);
    
    // IEEE address as hex string - now directly use the pre-formatted string from the device struct
    if (device->ieee_addr_str[0] == '\0') {
        // Fallback if string is not formatted (should not happen with new logic but good for safety)
        ESP_LOGW(LOG_TAG, "IEEE string for 0x%04x is not pre-formatted, formatting now.", device->short_addr);
        char temp_ieee_str[24];
        zigbee_format_ieee_addr_to_str(device->ieee_addr, temp_ieee_str, sizeof(temp_ieee_str));
        cJSON_AddStringToObject(json, "ieee_addr", temp_ieee_str);
    } else {
        cJSON_AddStringToObject(json, "ieee_addr", device->ieee_addr_str);
    }
    
    // Add other device properties
    cJSON_AddBoolToObject(json, "active", device->active);
    cJSON_AddNumberToObject(json, "last_seen", device->last_seen);
    
// Clean strings from control characters before adding to JSON
    char clean_device_name[32];
    char clean_manufacturer_name[32];
    
    strncpy(clean_device_name, device->device_name, sizeof(clean_device_name)-1);
    clean_device_name[sizeof(clean_device_name)-1] = '\0';
    clean_string(clean_device_name);
    
    strncpy(clean_manufacturer_name, device->manufacturer_name, sizeof(clean_manufacturer_name)-1);
    clean_manufacturer_name[sizeof(clean_manufacturer_name)-1] = '\0';
    clean_string(clean_manufacturer_name);
    
    cJSON_AddStringToObject(json, "device_name", clean_device_name);
    cJSON_AddStringToObject(json, "manufacturer_name", clean_manufacturer_name);
    cJSON_AddNumberToObject(json, "manufacturer_code", device->manufacturer_code);
    cJSON_AddNumberToObject(json, "power_source", device->power_source);
    cJSON_AddNumberToObject(json, "battery_voltage", device->battery_voltage);
    cJSON_AddNumberToObject(json, "battery_percentage", device->battery_percentage);
    cJSON_AddNumberToObject(json, "firmware_version", device->firmware_version);

    // Add endpoints array
    cJSON *endpoints = cJSON_AddArrayToObject(json, "endpoints");
    if (!endpoints) {
        ESP_LOGE(LOG_TAG, "Failed to create endpoints array");
        cJSON_Delete(json);
        return NULL;
    }

    // Add each endpoint
    for (int i = 0; i < device->endpoint_count; i++) {
        cJSON *ep = cJSON_CreateObject();
        if (!ep) {
            ESP_LOGE(LOG_TAG, "Failed to create endpoint object");
            cJSON_Delete(json);
            return NULL;
        }

        cJSON_AddNumberToObject(ep, "endpoint", device->endpoints[i].endpoint);
        cJSON_AddNumberToObject(ep, "profile_id", device->endpoints[i].profile_id);
        cJSON_AddNumberToObject(ep, "device_id", device->endpoints[i].device_id);

        // Add clusters array
        cJSON *clusters = cJSON_AddArrayToObject(ep, "clusters");
        if (!clusters) {
            ESP_LOGE(LOG_TAG, "Failed to create clusters array");
            cJSON_Delete(json);
            return NULL;
        }

        // Add each cluster
        for (int j = 0; j < device->endpoints[i].cluster_count; j++) {
            cJSON_AddItemToArray(clusters, cJSON_CreateNumber(device->endpoints[i].cluster_list[j]));
        }

        cJSON_AddItemToArray(endpoints, ep);
    }

    // Add report configurations array
    cJSON *reports = cJSON_AddArrayToObject(json, "reports");
    if (!reports) {
        ESP_LOGE(LOG_TAG, "Failed to create reports array");
        cJSON_Delete(json);
        return NULL;
    }

    // Add each report configuration
    for (int i = 0; i < MAX_REPORT_CFGS; i++) {
        if (device->report_cfgs[i].in_use) {
            cJSON *report = cJSON_CreateObject();
            if (!report) {
                ESP_LOGE(LOG_TAG, "Failed to create report object");
                cJSON_Delete(json);
                return NULL;
            }

            // Common fields
            cJSON_AddNumberToObject(report, "direction", device->report_cfgs[i].direction);
            cJSON_AddNumberToObject(report, "ep", device->report_cfgs[i].ep);
            cJSON_AddNumberToObject(report, "cluster_id", device->report_cfgs[i].cluster_id);
            cJSON_AddNumberToObject(report, "attr_id", device->report_cfgs[i].attr_id);

            if (device->report_cfgs[i].direction == REPORT_CFG_DIRECTION_SEND) {
                cJSON_AddNumberToObject(report, "attr_type", device->report_cfgs[i].send_cfg.attr_type);
                cJSON_AddNumberToObject(report, "min_int", device->report_cfgs[i].send_cfg.min_int);
                cJSON_AddNumberToObject(report, "max_int", device->report_cfgs[i].send_cfg.max_int);
                // Only add reportable_change_val if it's not the 'unused' marker
                if (device->report_cfgs[i].send_cfg.reportable_change_val != 0xFFFFFFFF) {
                    cJSON_AddNumberToObject(report, "reportable_change_val", device->report_cfgs[i].send_cfg.reportable_change_val);
                }
            } else if (device->report_cfgs[i].direction == REPORT_CFG_DIRECTION_RECV) {
                cJSON_AddNumberToObject(report, "timeout_period", device->report_cfgs[i].recv_cfg.timeout_period);
            }

            cJSON_AddItemToArray(reports, report);
        }
    }

    ESP_LOGI(LOG_TAG, "Successfully created JSON for device 0x%04x", device->short_addr);
    return json;
}

// Parse device from JSON object
esp_err_t device_from_json(const cJSON *json, zigbee_device_t *device, mp_obj_t zig_obj_mp) {
    if (!json || !device) {
        ESP_LOGE(LOG_TAG, "NULL parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clear device structure
    memset(device, 0, sizeof(zigbee_device_t));
    
    // Get short address as hex string
    cJSON *short_addr_item = cJSON_GetObjectItem(json, "short_addr"); // Renamed for clarity
    if (!cJSON_IsString(short_addr_item)) {
        ESP_LOGE(LOG_TAG, "Invalid short_addr type, expected string");
        return ESP_ERR_INVALID_ARG;
    }
    // Temporarily store parsed short_addr for logging before assigning to device->short_addr
    uint16_t parsed_short_addr_val = 0;
    if (sscanf(short_addr_item->valuestring, "0x%hx", &parsed_short_addr_val) != 1) {
        ESP_LOGE(LOG_TAG, "Failed to parse short_addr string: '%s'", short_addr_item->valuestring);
        return ESP_ERR_INVALID_ARG;
    }
    device->short_addr = parsed_short_addr_val; // Assign after successful parse
    
    // Get IEEE address as MAC-style string with colons
    cJSON *ieee_addr_item = cJSON_GetObjectItem(json, "ieee_addr"); 
    if (!cJSON_IsString(ieee_addr_item)) {
        ESP_LOGE(LOG_TAG, "Invalid ieee_addr type for 0x%04x, expected string", device->short_addr);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(LOG_TAG, "JSON PARSE: Attempting to process device from JSON: short_addr_str='%s' (parsed 0x%04x), ieee_addr_str='%s'", 
             short_addr_item->valuestring, device->short_addr, ieee_addr_item->valuestring);

    // Parse MAC address format (xx:xx:xx:xx:xx:xx:xx:xx)
    if (!zigbee_parse_ieee_str_to_addr(ieee_addr_item->valuestring, device->ieee_addr)) {
        ESP_LOGE(LOG_TAG, "Failed to parse ieee_addr string: '%s' for short_addr 0x%04x", 
                 ieee_addr_item->valuestring, device->short_addr);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Call device_manager_add to handle potential new device or update short_addr for existing IEEE
    // This is now called unconditionally for every JSON entry during initial load.
    // device_manager_add will itself decide if a new entry is needed in the list or if an existing one is updated.
    esp_err_t add_err = device_manager_add(device->short_addr, device->ieee_addr, zig_obj_mp, true);
    if (add_err != ESP_OK && add_err != ESP_ERR_NO_MEM && add_err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_NO_MEM means list is full. ESP_ERR_INVALID_STATE can mean non-critical conflict handled by add.
        // Other errors are more critical for parsing this JSON.
        ESP_LOGE(LOG_TAG, "device_manager_add failed during device_from_json for 0x%04x (IEEE: %s) with error: %s. JSON data might not be fully applied.",
                 device->short_addr, ieee_addr_item->valuestring, esp_err_to_name(add_err));
        // Depending on severity, might want to return add_err here
        // For now, let it proceed to device_manager_update which will target the prepared slot or existing device.
    } else if (add_err == ESP_ERR_NO_MEM) {
        ESP_LOGE(LOG_TAG, "Device list full, cannot process JSON for 0x%04x (IEEE: %s)", device->short_addr, ieee_addr_item->valuestring);
        return ESP_ERR_NO_MEM; // Hard fail if list is full
    }
    // If add_err is ESP_ERR_INVALID_STATE, it means device_manager_add handled a conflict (e.g. short_addr taken by different IEEE)
    // and decided not to add/update short_addr. We can still proceed to update other attributes if the device (by old short_addr or IEEE) exists.
        
    // Get basic info with safe defaults
    cJSON *active = cJSON_GetObjectItem(json, "active");
    device->active = cJSON_IsBool(active) ? cJSON_IsTrue(active) : false;
    
    cJSON *last_seen = cJSON_GetObjectItem(json, "last_seen");
    device->last_seen = cJSON_IsNumber(last_seen) ? (uint32_t)last_seen->valuedouble : 0;
    
    // Get string fields with bounds checking
    cJSON *device_name = cJSON_GetObjectItem(json, "device_name");
    
    if (cJSON_IsString(device_name)) {
        strncpy(device->device_name, device_name->valuestring, sizeof(device->device_name)-1);
        device->device_name[sizeof(device->device_name)-1] = '\0';
    }
    
    cJSON *manufacturer_name = cJSON_GetObjectItem(json, "manufacturer_name");
    if (cJSON_IsString(manufacturer_name)) {
        strncpy(device->manufacturer_name, manufacturer_name->valuestring, 
                sizeof(device->manufacturer_name)-1);
        device->manufacturer_name[sizeof(device->manufacturer_name)-1] = '\0';
    }
    
    // Get numeric fields with validation
    cJSON *manufacturer_code = cJSON_GetObjectItem(json, "manufacturer_code");
    device->manufacturer_code = cJSON_IsNumber(manufacturer_code) ? 
                              (uint16_t)manufacturer_code->valuedouble : 0;
    
    cJSON *power_source = cJSON_GetObjectItem(json, "power_source");
    device->power_source = cJSON_IsNumber(power_source) ? 
                          (uint8_t)power_source->valuedouble : 0;
    
    cJSON *battery_voltage = cJSON_GetObjectItem(json, "battery_voltage");
    device->battery_voltage = cJSON_IsNumber(battery_voltage) ? 
                            (uint16_t)battery_voltage->valuedouble : 0;
    
    cJSON *battery_percentage = cJSON_GetObjectItem(json, "battery_percentage");
    device->battery_percentage = cJSON_IsNumber(battery_percentage) ? 
                               (uint8_t)battery_percentage->valuedouble : 0;
    
    cJSON *firmware_version = cJSON_GetObjectItem(json, "firmware_version");
    device->firmware_version = cJSON_IsNumber(firmware_version) ? 
                             (uint32_t)firmware_version->valuedouble : 0;
    
    // Get endpoints
    cJSON *endpoints = cJSON_GetObjectItem(json, "endpoints");
    if (!cJSON_IsArray(endpoints)) {
        ESP_LOGE(LOG_TAG, "Invalid endpoints array");
        return ESP_ERR_INVALID_ARG;
    }
    
    device->endpoint_count = 0;
    int ep_count = cJSON_GetArraySize(endpoints);
    
    for (int i = 0; i < ep_count && i < MAX_ENDPOINTS; i++) {
        cJSON *ep = cJSON_GetArrayItem(endpoints, i);
        if (!cJSON_IsObject(ep)) continue;
        
        cJSON *endpoint = cJSON_GetObjectItem(ep, "endpoint");
        cJSON *profile_id = cJSON_GetObjectItem(ep, "profile_id");
        cJSON *device_id = cJSON_GetObjectItem(ep, "device_id");
        
        if (!cJSON_IsNumber(endpoint) || !cJSON_IsNumber(profile_id) || 
            !cJSON_IsNumber(device_id)) {
            ESP_LOGW(LOG_TAG, "Invalid endpoint %d data", i);
            continue;
        }
        
        device->endpoints[i].endpoint = (uint8_t)endpoint->valuedouble;
        device->endpoints[i].profile_id = (uint16_t)profile_id->valuedouble;
        device->endpoints[i].device_id = (uint16_t)device_id->valuedouble;
        
        // Get clusters
        cJSON *clusters = cJSON_GetObjectItem(ep, "clusters");
        if (cJSON_IsArray(clusters)) {
            device->endpoints[i].cluster_count = 0;
            int cluster_count = cJSON_GetArraySize(clusters);
            
            for (int j = 0; j < cluster_count && j < 32; j++) {
                cJSON *cluster = cJSON_GetArrayItem(clusters, j);
                if (cJSON_IsNumber(cluster)) {
                    device->endpoints[i].cluster_list[j] = (uint16_t)cluster->valuedouble;
                    device->endpoints[i].cluster_count++;
                }
            }
        }
        
        device->endpoint_count++;
    }
    
    // Get report configurations
    cJSON *reports = cJSON_GetObjectItem(json, "reports");
    if (cJSON_IsArray(reports)) {
        // Reset all configs
        for (int i = 0; i < MAX_REPORT_CFGS; i++) {
            device->report_cfgs[i].in_use = false;
        }
        
        int report_count = cJSON_GetArraySize(reports);
        for (int i = 0; i < report_count && i < MAX_REPORT_CFGS; i++) {
            cJSON *report = cJSON_GetArrayItem(reports, i);
            if (!cJSON_IsObject(report)) continue;
            
            cJSON *ep_json = cJSON_GetObjectItem(report, "ep");
            cJSON *cluster_id_json = cJSON_GetObjectItem(report, "cluster_id");
            cJSON *attr_id_json = cJSON_GetObjectItem(report, "attr_id");
            cJSON *direction_json = cJSON_GetObjectItem(report, "direction");

            if (!cJSON_IsNumber(ep_json) || !cJSON_IsNumber(cluster_id_json) || 
                !cJSON_IsNumber(attr_id_json) || !cJSON_IsNumber(direction_json)) {
                ESP_LOGW(LOG_TAG, "Invalid common report config fields for report %d", i);
                continue;
            }
            
            device->report_cfgs[i].in_use = true;
            device->report_cfgs[i].direction = (uint8_t)direction_json->valuedouble;
            device->report_cfgs[i].ep = (uint8_t)ep_json->valuedouble;
            device->report_cfgs[i].cluster_id = (uint16_t)cluster_id_json->valuedouble;
            device->report_cfgs[i].attr_id = (uint16_t)attr_id_json->valuedouble;

            if (device->report_cfgs[i].direction == REPORT_CFG_DIRECTION_SEND) {
                cJSON *attr_type_json = cJSON_GetObjectItem(report, "attr_type");
                cJSON *min_int_json = cJSON_GetObjectItem(report, "min_int");
                cJSON *max_int_json = cJSON_GetObjectItem(report, "max_int");
                cJSON *rc_val_json = cJSON_GetObjectItem(report, "reportable_change_val"); // Optional

                if (!cJSON_IsNumber(attr_type_json) || !cJSON_IsNumber(min_int_json) || !cJSON_IsNumber(max_int_json)) {
                    ESP_LOGW(LOG_TAG, "Invalid send_cfg fields for report %d", i);
                    device->report_cfgs[i].in_use = false; // Mark as invalid
                    continue;
                }
                device->report_cfgs[i].send_cfg.attr_type = (uint8_t)attr_type_json->valuedouble;
                device->report_cfgs[i].send_cfg.min_int = (uint16_t)min_int_json->valuedouble;
                device->report_cfgs[i].send_cfg.max_int = (uint16_t)max_int_json->valuedouble;
                if (rc_val_json && cJSON_IsNumber(rc_val_json)) {
                    device->report_cfgs[i].send_cfg.reportable_change_val = (uint32_t)rc_val_json->valuedouble;
                } else {
                    device->report_cfgs[i].send_cfg.reportable_change_val = 0xFFFFFFFF; // Mark as not set
                }
            } else if (device->report_cfgs[i].direction == REPORT_CFG_DIRECTION_RECV) {
                cJSON *timeout_json = cJSON_GetObjectItem(report, "timeout_period");
                if (!cJSON_IsNumber(timeout_json)) {
                    ESP_LOGW(LOG_TAG, "Invalid recv_cfg fields for report %d", i);
                    device->report_cfgs[i].in_use = false; // Mark as invalid
                    continue;
                }
                device->report_cfgs[i].recv_cfg.timeout_period = (uint16_t)timeout_json->valuedouble;
            } else {
                ESP_LOGW(LOG_TAG, "Unknown direction %d for report %d", device->report_cfgs[i].direction, i);
                device->report_cfgs[i].in_use = false; // Mark as invalid
                continue;
            }
        }
    }
    
    ESP_LOGI(LOG_TAG, "Successfully parsed device 0x%04x from JSON", device->short_addr);
    return ESP_OK;
}
