// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "device_json.h"
#include "device_manager.h"

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
    
    // IEEE address as hex string
    char ieee_addr_str[24];
    snprintf(ieee_addr_str, sizeof(ieee_addr_str), 
             "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             device->ieee_addr[7], device->ieee_addr[6],
             device->ieee_addr[5], device->ieee_addr[4],
             device->ieee_addr[3], device->ieee_addr[2],
             device->ieee_addr[1], device->ieee_addr[0]);
    cJSON_AddStringToObject(json, "ieee_addr", ieee_addr_str);
    
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

            cJSON_AddNumberToObject(report, "ep", device->report_cfgs[i].ep);
            cJSON_AddNumberToObject(report, "cluster_id", device->report_cfgs[i].cluster_id);
            cJSON_AddNumberToObject(report, "attr_id", device->report_cfgs[i].attr_id);
            cJSON_AddNumberToObject(report, "attr_type", device->report_cfgs[i].attr_type);
            cJSON_AddNumberToObject(report, "min_int", device->report_cfgs[i].min_int);
            cJSON_AddNumberToObject(report, "max_int", device->report_cfgs[i].max_int);
            cJSON_AddNumberToObject(report, "timeout", device->report_cfgs[i].timeout);

            cJSON_AddItemToArray(reports, report);
        }
    }

    ESP_LOGI(LOG_TAG, "Successfully created JSON for device 0x%04x", device->short_addr);
    return json;
}

// Parse device from JSON object
esp_err_t device_from_json(const cJSON *json, zigbee_device_t *device) {
    if (!json || !device) {
        ESP_LOGE(LOG_TAG, "NULL parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clear device structure
    memset(device, 0, sizeof(zigbee_device_t));
    
    // Get short address as hex string
    cJSON *short_addr = cJSON_GetObjectItem(json, "short_addr");
    if (!cJSON_IsString(short_addr)) {
        ESP_LOGE(LOG_TAG, "Invalid short_addr type, expected string");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sscanf(short_addr->valuestring, "0x%hx", &device->short_addr) != 1) {
        ESP_LOGE(LOG_TAG, "Failed to parse short_addr");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get IEEE address as MAC-style string with colons
    cJSON *ieee_addr = cJSON_GetObjectItem(json, "ieee_addr");
    if (!cJSON_IsString(ieee_addr)) {
        ESP_LOGE(LOG_TAG, "Invalid ieee_addr type, expected string");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Parse MAC address format (xx:xx:xx:xx:xx:xx:xx:xx)
    unsigned int bytes[8];
    if (sscanf(ieee_addr->valuestring, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3],
               &bytes[4], &bytes[5], &bytes[6], &bytes[7]) != 8) {
        ESP_LOGE(LOG_TAG, "Failed to parse ieee_addr");
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < 8; i++) {
        device->ieee_addr[i] = (uint8_t)bytes[i];
    }
    
    // Get basic info with safe defaults
    cJSON *active = cJSON_GetObjectItem(json, "active");
    device->active = cJSON_IsBool(active) ? cJSON_IsTrue(active) : false;
    
    cJSON *last_seen = cJSON_GetObjectItem(json, "last_seen");
    device->last_seen = cJSON_IsNumber(last_seen) ? (uint32_t)last_seen->valuedouble : 0;
    
    // Get string fields with bounds checking
    cJSON *device_name = cJSON_GetObjectItem(json, "device_name");
    
    // Check if device exists, if not - add it
    zigbee_device_t *existing = device_manager_get(device->short_addr);
    if (!existing) {
        // Convert IEEE address to uint64_t
        uint64_t ieee_addr = 0;
        for (int i = 0; i < 8; i++) {
            ieee_addr |= (uint64_t)device->ieee_addr[i] << (i * 8);
        }
        esp_err_t err = device_manager_add(device->short_addr, ieee_addr);
        if (err != ESP_OK) {
            ESP_LOGE(LOG_TAG, "Failed to add device 0x%04x: %s", device->short_addr, esp_err_to_name(err));
            return err;
        }
    }
    
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
            
            cJSON *ep = cJSON_GetObjectItem(report, "ep");
            cJSON *cluster_id = cJSON_GetObjectItem(report, "cluster_id");
            cJSON *attr_id = cJSON_GetObjectItem(report, "attr_id");
            cJSON *min_int = cJSON_GetObjectItem(report, "min_int");
            cJSON *max_int = cJSON_GetObjectItem(report, "max_int");
            
            if (!cJSON_IsNumber(ep) || !cJSON_IsNumber(cluster_id) || 
                !cJSON_IsNumber(attr_id) || !cJSON_IsNumber(min_int) || 
                !cJSON_IsNumber(max_int)) {
                ESP_LOGW(LOG_TAG, "Invalid report config %d", i);
                continue;
            }
            
            device->report_cfgs[i].in_use = true;
            device->report_cfgs[i].ep = (uint8_t)ep->valuedouble;
            device->report_cfgs[i].cluster_id = (uint16_t)cluster_id->valuedouble;
            device->report_cfgs[i].attr_id = (uint16_t)attr_id->valuedouble;
            device->report_cfgs[i].min_int = (uint16_t)min_int->valuedouble;
            device->report_cfgs[i].max_int = (uint16_t)max_int->valuedouble;
        }
    }
    
    ESP_LOGI(LOG_TAG, "Successfully parsed device 0x%04x from JSON", device->short_addr);
    return ESP_OK;
}
