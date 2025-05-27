// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP-IDF headers
#include "esp_log.h"
#include "esp_check.h"
#include "esp_partition.h"  // For working with partitions

#include "esp_idf_version.h"
#include "esp_err.h"
#include "esp_task.h"

//?
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"

//ZigBee
#include "esp_zigbee_core.h"        // Main header of esp-zigbee-lib
#include "esp_zigbee_attribute.h"   // For working with attributes
#include "esp_zigbee_type.h"        // Data types
#include "esp_radio_spinel.h"       // For working with RCP
#include "platform/esp_zigbee_platform.h"    // For working with platform

// MicroPython headers
#include "mpconfigport.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "py/mperrno.h"
// Add threading support for releasing GIL in commissioning task
#include "py/mpthread.h"

// mod_zig_core.c
#include "mod_zig_core.h"
#include "mod_zig_handlers.h"
#include "mod_zig_custom.h"  // Adding header file inclusion
#include "main.h"  // Adding for access to constants

static const char *TAG = "ZIGBEE_CORE";

#define ZIGBEE_TASK_CORE  (MP_TASK_COREID ^ 1)

static const uint32_t esp32_zig_gateway_task_stack = 8192; // Stack size for Zigbee gateway task
static const uint32_t esp32_zig_commissioning_task_stack = 8192; // Stack size for Zigbee commissioning task

static const uint32_t esp32_zig_gateway_task_priority = 5; // Priority for Zigbee gateway task
static const uint32_t esp32_zig_commissioning_task_priority = 5; // Priority for Zigbee commissioning task

// Global variable for cluster list
esp_zb_cluster_list_t *cluster_list = NULL;

// Global instance of the main Zigbee object for MicroPython
//esp32_zig_obj_t esp32_zig_obj; 
// Global pointer to the main Zigbee object, initialized in esp_zb_app_init
esp32_zig_obj_t *zb_obj = &esp32_zig_obj;

// Global variable for endpoint list
static esp_zb_ep_list_t *global_ep_list = NULL;


// Need for micropython compatibility
extern BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
    const char * const pcName,
    const uint32_t usStackDepth,    void * const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t * const pxCreatedTask,
    const BaseType_t xCoreID);

// Function to initialize Zigbee platform using high-level API
esp_err_t init_zigbee_platform(uint8_t uart_port, 
                               uint8_t rx_pin, uint8_t tx_pin, uint32_t baud_rate,
                               uint8_t reset_pin, uint8_t boot_pin)
{
    ESP_LOGI(TAG, "ZIGBEE: Initializing Zigbee platform with parameters: UART=%d, RX=%d, TX=%d, speed=%lu", 
             uart_port, rx_pin, tx_pin, (unsigned long)baud_rate);
    
    // Check parameters
    if (rx_pin == 0 || tx_pin == 0) {
        ESP_LOGI(TAG, "ZIGBEE: Error! Invalid RX/TX pin values ");
        return ESP_ERR_INVALID_ARG;
    }
    
    // DO NOT configure and initialize UART yourself
    // Just create a configuration for the ESP-Zigbee library

    
    // Create a Zigbee platform configuration
    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_UART_RCP,
            .radio_uart_config = {
                .port = uart_port,
                .rx_pin = rx_pin,
                .tx_pin = tx_pin,
                .uart_config = {
                    .baud_rate = baud_rate,
                    .data_bits = UART_DATA_8_BITS,
                    .parity = UART_PARITY_DISABLE,
                    .stop_bits = UART_STOP_BITS_1,
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
                }
            }
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE
        }
    };
    
    ESP_LOGI(TAG, "ZIGBEE: Configuring Zigbee platform ");
    
    // Apply platform configuration
    esp_err_t ret = esp_zb_platform_config(&platform_config);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "ZIGBEE: Error initializing Zigbee platform: %s ", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ZIGBEE: Zigbee platform initialization started, waiting 1000ms ");
    
    // Give time for RCP initialization
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "ZIGBEE: Zigbee platform initialization completed ");
    
    return ESP_OK;
}

// Check if Zigbee partition exists in partition table
esp_err_t check_zigbee_partitions(void) {

    const esp_partition_t *zb_storage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "zb_storage");
    if (zb_storage == NULL) {
        ESP_LOGI(TAG, "ZIGBEE: Error! zb_storage partition not found in partition table");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ZIGBEE: zb_storage partition found: size %u bytes, offset 0x%x", (unsigned)zb_storage->size, (unsigned)zb_storage->address);
    
    const esp_partition_t *zb_fct = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "zb_fct");
    if (zb_fct == NULL) {
        ESP_LOGI(TAG, "ZIGBEE: Error! zb_fct partition not found in partition table");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ZIGBEE: zb_fct partition found: size %u bytes, offset 0x%x", (unsigned)zb_fct->size, (unsigned)zb_fct->address);
    
    return ESP_OK;
}

// Check RCP version
static esp_err_t check_rcp_version(void)
{
    ESP_LOGI(TAG, "ZIGBEE: Getting RCP version... ");
    
    char internal_rcp_version[128];
    esp_err_t ret;

    memset(internal_rcp_version, 0, sizeof(internal_rcp_version));

    //Protect against possible errors
    //MP_THREAD_GIL_EXIT();
    ret = esp_radio_spinel_rcp_version_get(internal_rcp_version, ESP_RADIO_SPINEL_ZIGBEE);
   // MP_THREAD_GIL_ENTER();

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "  ZIGBEE: Error: Failed to get RCP version: ");
        ESP_LOGI(TAG, "   - Error code: %d ", ret);
        ESP_LOGI(TAG, "   - Error message: %s ", esp_err_to_name(ret));
        ESP_LOGI(TAG, "   - Possible reasons: ");
        ESP_LOGI(TAG, "     1. RCP does not respond to commands ");
        ESP_LOGI(TAG, "     2. Incorrect RCP firmware ");
        ESP_LOGI(TAG, "     3. Communication issues via UART  ");
        return ret;
    }

    if (internal_rcp_version[0] == '\0') {
        ESP_LOGI(TAG, "  ZIGBEE: Error: RCP version is empty ");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "  ZIGBEE: RCP version received! ");
    ESP_LOGI(TAG, "   - Version: %s ", internal_rcp_version);
    ESP_LOGI(TAG, "   - Status: Device is ready to work  ");
    return ESP_OK;
}

// Task for executing the main Zigbee event loop
void esp_zb_gateway_task(void *pvParameters)
{

    ESP_LOGI(TAG, "GTW:Task: Zigbee gateway task started in async mode ");

    while (1) {
        esp_zb_stack_main_loop_iteration();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

}

// Function for initializing Zigbee gateway
esp_err_t init_zigbee_gateway(esp32_zig_obj_t *self) {
    ESP_LOGI(TAG, "GATEWAY:INIT: Initializing Zigbee gateway ");
    
    // Check if Zigbee partitions exist in the partition table
    esp_err_t ret = check_zigbee_partitions();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "GATEWAY:INIT: Error! Required Zigbee partitions not found in partition table");
        return ret;
    }
    

    
    // Initialize Zigbee platform using high-level API
    ESP_LOGI(TAG, "GATEWAY:INIT: Initializing Zigbee platform for gateway");

    
    // Use settings from configuration
    ret = init_zigbee_platform(
        self->config->uart_port, 
        self->config->uart_rx_pin, 
        self->config->uart_tx_pin,  
        self->config->bitrate,  
        self->config->rcp_reset_pin,  
        self->config->rcp_boot_pin    
    );
    
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "GATEWAY:INIT: Error initializing Zigbee platform: %s", esp_err_to_name(ret));
        return ret;
    }


    ESP_LOGI(TAG, "GATEWAY:INIT: Zigbee platform successfully initialized");
    
    // Register RCP failure handler
    ESP_LOGI(TAG, "GATEWAY:INIT: Registering RCP failure handler");
    esp_radio_spinel_register_rcp_failure_handler(rcp_error_handler, ESP_RADIO_SPINEL_ZIGBEE);


    // Initialize Zigbee stack
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,
        .nwk_cfg.zczr_cfg = {
            .max_children = MAX_CHILDREN
        }
    };
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Initializing Zigbee stack");
    esp_zb_init(&zb_nwk_cfg);

    ESP_ERROR_CHECK(check_rcp_version());


    ESP_LOGI(TAG, "GATEWAY:INIT: Setting primary channel");
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    // Create endpoint list and cluster list
    ESP_LOGI(TAG, "ZIGBEE: Creating endpoint list and cluster list");
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    global_ep_list = ep_list;
    cluster_list = esp_zb_zcl_cluster_list_create();  // Using global variable


    ESP_LOGI(TAG, "GATEWAY:INIT: Configuring endpoint ");
    // Configure endpoint
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID, // Use REMOTE_CONTROL_DEVICE_ID
        .app_device_version = 0,
    };

    // Add basic cluster with manufacturer information
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (uint8_t *)ESP_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (uint8_t *)ESP_MODEL_IDENTIFIER);
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Adding clusters to the endpoint ");
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    // Add Identify cluster as server on gateway endpoint
    {
        esp_zb_attribute_list_t *identify_cluster = esp_zb_identify_cluster_create(NULL);
        esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

    // Add Basic cluster as client to support Read Attributes commands
    {
        esp_zb_attribute_list_t *basic_client = esp_zb_basic_cluster_create(NULL);
        esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }


    // Add Time cluster to gateway endpoint (server role) per example
    {
        // Time cluster for time sync (server role)
        esp_zb_attribute_list_t *time_cluster = esp_zb_time_cluster_create(NULL);
        esp_zb_cluster_list_add_time_cluster(cluster_list, time_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        // Time cluster for reading time (client role)
        esp_zb_attribute_list_t *time_client = esp_zb_time_cluster_create(NULL);
        esp_zb_cluster_list_add_time_cluster(cluster_list, time_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }
    // Add On/Off cluster as client to receive state reports
    {
        esp_zb_on_off_cluster_cfg_t onoff_cfg = { .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE };
        esp_zb_attribute_list_t *onoff_client = esp_zb_on_off_cluster_create(&onoff_cfg);
        esp_zb_cluster_list_add_on_off_cluster(cluster_list, onoff_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }
    // Add IAS Zone cluster as client for alarm/sensor reports
    {
        esp_zb_ias_zone_cluster_cfg_t ias_cfg = {0};
        esp_zb_attribute_list_t *ias_client = esp_zb_ias_zone_cluster_create(&ias_cfg);
        esp_zb_cluster_list_add_ias_zone_cluster(cluster_list, ias_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }
    // Add Power Configuration cluster as client to read battery attributes
    {
        esp_zb_power_config_cluster_cfg_t power_cfg = {0};
        esp_zb_attribute_list_t *power_client = esp_zb_power_config_cluster_create(&power_cfg);
        esp_zb_cluster_list_add_power_config_cluster(cluster_list, power_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }    
    
    // Add OTA Upgrade cluster as client to query firmware updates
    {
        esp_zb_ota_cluster_cfg_t ota_cfg = {0};
        esp_zb_attribute_list_t *ota_client = esp_zb_ota_cluster_create(&ota_cfg);
        esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    }
    

    
    ESP_LOGI(TAG, "GATEWAY:INIT: Initializing custom clusters");
    // Create and register custom cluster handlers
    custom_clusters_init();
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Adding endpoint to endpoint list ");
    // Add endpoint to endpoint list
    esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config);
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Registering device ");
    // Register device
    esp_zb_device_register(ep_list);
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Setting manufacturer code ");
    // Set the manufacturer code
    esp_zb_set_node_descriptor_manufacturer_code(ESP_MANUFACTURER_CODE);
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Starting Zigbee stack ");
    // Register handlers for unhandled Zigbee commands and events
    esp_zb_raw_command_handler_register(zb_raw_cmd_handler);
    esp_zb_core_action_handler_register(zb_action_handler);
    ESP_LOGI(TAG, "GATEWAY:INIT: Registering custom cluster handlers ");

    
// Starting Zigbee stack
    ret = esp_zb_start(false);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "GATEWAY:INIT: Error starting Zigbee stack: %s ", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "GATEWAY:INIT: Zigbee stack successfully started ");
    return ESP_OK;
}


// Commissioning task: run init_zigbee_gateway in background
static void zigbee_commissioning_task(void *pvParameters) {
    esp32_zig_obj_t *self = (esp32_zig_obj_t*)pvParameters;
    // Release Python GIL while blocking operations run
    MP_THREAD_GIL_EXIT();
    esp_err_t err = init_zigbee_gateway(self);
    
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "COMISSIONING: Commissioning error: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "COMISSIONING: Commissioning completed, wont starting main Zigbee Task");
    // Start main Zigbee event loop task
    BaseType_t rc = xTaskCreatePinnedToCore(
        esp_zb_gateway_task,
        "zigbee_gateway",
        esp32_zig_gateway_task_stack,
        NULL,
        esp32_zig_gateway_task_priority,
        &self->gateway_task,
        ZIGBEE_TASK_CORE // MP_TASK_COREID
    );
    if (rc != pdPASS) {
        ESP_LOGI(TAG, "COMISSIONING: Failed to start main Zigbee Task");
    }
    ESP_LOGI(TAG, "COMISSIONING: Commissioning completed, starting main Zigbee Task");
    vTaskDelete(NULL);
}



// Helper: schedule commissioning in background
esp_err_t esp32_zig_start_gateway(esp32_zig_obj_t *self) {
    // Schedule commissioning task without blocking Python
    
    BaseType_t created = xTaskCreatePinnedToCore(
        zigbee_commissioning_task,                      // Task function
        "zigbee_comm",                                  // Task name
        esp32_zig_commissioning_task_stack,             // Stack size
        self,                                           // Pass self pointer
        esp32_zig_commissioning_task_priority,          // Priority
        &self->gateway_task,                            // Handle
        ZIGBEE_TASK_CORE                                // MP_TASK_COREID    // Core to run
    );
    return (created == pdPASS) ? ESP_OK : ESP_FAIL;
}



// reset_to_factory() - Reset the Zigbee gateway to factory settings
static mp_obj_t esp32_zig_reset_to_factory(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Check if network is formed
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Network is not formed");
        return mp_const_none;
    }
    
    
    ESP_LOGI(TAG, "ZIGBEE: Starting factory reset process... ");
    
    // 1. First close the network
    ZB_LOCK();
    esp_err_t ret = esp_zb_bdb_close_network();
    ZB_UNLOCK();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "ZIGBEE: Failed to close network: %s", esp_err_to_name(ret));
    }
    
    // 2. Update object state
    self->config->network_formed = false;
    self->config->pan_id = 0;
    self->config->channel = 0;
    
    ESP_LOGI(TAG, "ZIGBEE: Performing factory reset... ");
    
    // 3. Perform factory reset
// This function will clear zb_storage (including security keys) and reboot the device
    ZB_LOCK();
    esp_zb_factory_reset();
    ZB_UNLOCK();
    
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_reset_to_factory_obj, esp32_zig_reset_to_factory);

// Utility function to format an IEEE address byte array into a string
void zigbee_format_ieee_addr_to_str(const uint8_t ieee_addr[8], char *out_str, size_t out_str_len) {
    if (!out_str || out_str_len < 24) { // Minimum length for "XX:XX:XX:XX:XX:XX:XX:XX\0"
        return; 
    }
    snprintf(out_str, out_str_len,
             "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             ieee_addr[0], ieee_addr[1], ieee_addr[2], ieee_addr[3],
             ieee_addr[4], ieee_addr[5], ieee_addr[6], ieee_addr[7]);
}

// Utility function to parse an IEEE address string into a byte array
bool zigbee_parse_ieee_str_to_addr(const char *ieee_str, uint8_t out_addr[8]) {
    if (!ieee_str || !out_addr) {
        return false;
    }
    unsigned int bytes[8];
    if (sscanf(ieee_str, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3],
               &bytes[4], &bytes[5], &bytes[6], &bytes[7]) == 8) {
        for (int i = 0; i < 8; i++) {
            out_addr[i] = (uint8_t)bytes[i];
        }
        return true;
    }
    return false;
}