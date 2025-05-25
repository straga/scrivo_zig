// Copyright (c) 2025 Viktor Vorobjov
// This file contains the functions for device management
#include <string.h>

// MicroPython headers
#include "mpconfigport.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mperrno.h"

#include "esp_log.h"

//Project headers
#include "main.h"
#include "mod_zig_cmd.h"
#include "mod_zig_handlers.h"
#include "device_manager.h"
#include "mod_zig_custom.h"

#include "mod_zig_msg.h"
#include "mod_zig_core.h"

#define CUSTOM_TAG "ZB_CUSTOM"


// Handler callbacks for Tuya custom cluster
static esp_err_t tuya_check_value_handler(uint16_t attr_id, uint8_t endpoint, uint8_t *value) {
    // Allow all values by default
    return ESP_OK;
}

static void tuya_write_attr_handler(uint8_t endpoint, uint16_t attr_id, uint8_t *new_value, uint16_t manuf_code) {
    // Log write operations
    ESP_LOGI(CUSTOM_TAG, "Tuya cluster attr write: ep=%d, attr=0x%04x", endpoint, attr_id);
}

// Custom clusters initialization
esp_err_t custom_clusters_init(void) {
    esp_err_t err;
    // Use existing cluster list from core
    extern esp_zb_cluster_list_t *cluster_list;  // Declare external variable

    if (cluster_list == NULL) {  
        ESP_LOGE(CUSTOM_TAG, "Failed get cluster list");
        return ESP_ERR_NO_MEM;
    }

    // Create handlers for custom cluster
    esp_zb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = CUSTOM_CLUSTER_CLI_ID,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        .check_value_cb = tuya_check_value_handler,
        .write_attr_cb = tuya_write_attr_handler
    };

    // Register handlers
    err = esp_zb_zcl_custom_cluster_handlers_update(handlers);
    if (err != ESP_OK) {
        ESP_LOGE(CUSTOM_TAG, "Failed to register custom cluster handler: %s", esp_err_to_name(err));
        return err;
    }

    // Create custom cluster for Tuya devices (0xEF00)
    esp_zb_attribute_list_t *custom_cluster = esp_zb_zcl_attr_list_create(CUSTOM_CLUSTER_CLI_ID);
    if (!custom_cluster) {
        ESP_LOGE(CUSTOM_TAG, "Failed to create custom cluster");
        return ESP_ERR_NO_MEM;
    }

    // Add cluster to the list as client role since we're communicating with Tuya devices
    err = esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(CUSTOM_TAG, "Failed to add cluster to list: %s", esp_err_to_name(err));
        free(custom_cluster); // Clean up
        return err;
    }

    ESP_LOGI(CUSTOM_TAG, "ZB_CUSTOM: Custom clusters initialized");
    return ESP_OK;
}



